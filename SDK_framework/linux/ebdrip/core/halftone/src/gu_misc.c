/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:gu_misc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Miscellaneous halftone functions.
 */

#include "core.h"
#include "swoften.h"
#include "swerrors.h"
#include "swcopyf.h"
#include "objects.h"
#include "fileio.h"
#include "rsd.h"          /* rsdSetCircularFlag */
#include "monitor.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"
#include "objmatch.h"
#include "debugging.h"
#include "asyncps.h"
#include "progupdt.h"
#include "hqmemset.h"

#include "std_file.h"
#include "often.h"
#include "matrix.h"
#include "constant.h"     /* EPSILON */
#include "mathfunc.h"     /* NORMALISE_ANGLE */
#include "params.h"
#include "miscops.h"
#include "stacks.h"
#include "dicthash.h"
#include "graphics.h"
#include "gs_color.h"     /* GS_COLORinfo */
#include "gschtone.h"     /* gsc_getHalftonePhaseX */
#include "halftone.h"
#include "chalftone.h"
#include "control.h"
#include "gstate.h"
#include "gu_prscn.h"
#include "stackops.h"
#include "gu_misc.h"
#include "functns.h"
#include "gs_spotfn.h"
#include "gu_hsl.h"

#include "spdetect.h"
#include "rcbcntrl.h"
#include "dlstate.h" /* inputpage */

#include "hpscreen.h"     /* for cells struct and HPS smoothing code */
#include "gu_chan.h"      /* guc_overrideScreenAngle */
#include "security.h"

#include "hqxcrypt.h"

#include "gu_htm.h"       /* htm_SelectHalftone */


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
static Bool debug_newhalftone;
#endif

#if defined( DEBUG_BUILD )
static Bool debug_newthreshold = FALSE ;
#endif

/* forward declarations for 'C'spot functions */
#define none_sf                 0
#define round_sf                1
#define euclidean_sf            2
#define elliptical_sf1          3
#define elliptical_sf2          4
#define line_sf                 5
#define line90_sf               6
#define square_sf1              7
#define square_sf2              8
#define ellipticalQ_sf1         9
#define ellipticalQ_sf2         10
#define ellipticalP_sf          11
#define rhomboid_sf             12
#define invertedsimpledot_sf    13
#define diamond_sf              14
#define ellipse_sf              15
#define ellipsea_sf             16
#define invertedellipsea_sf     17
#define ellipseb_sf             18
#define invertedellipseb_sf     19
#define ellipsec_sf             20
#define invertedellipsec_sf     21
#define ellipsecadobe_sf        22
#define lineadobe_sf            23
#define linex_sf                24
#define liney_sf                25
#define square_sf               26
#define cross_sf                27
#define rhomboidadobe_sf        28
#define doubledot_sf            29
#define inverteddoubledot_sf    30
#define cosinedot_sf            31
#define double_sf               32
#define inverteddouble_sf       33

/* These are only used when we want to create the caches in house.
   The numbers have to be different from the ones above.
*/
#define dispersed_sf     SPOT_FUN_RAND
#define micro_sf         SPOT_FUN_RESPI
#define chain_sf         SPOT_FUN_CHAIN

/* Define the largest amount of memory that we'll allow for a single
   screen to fit into. This is to an extent aribtary, since all we're
   using it for is to stop real big bad overflow.
*/
#define MAX_SCREEN_MEMORY ((SYSTEMVALUE)(64 * 1024 * 1024))  /* 64 Meg */

/* Maximum number of threshold values in a Type 16 halftone. */
#define MAXT16LEVEL 0xffffu

/* This MUST be the maximum colourvalue level. */
#define MAXTHXSIZE COLORVALUE_MAX

/* lookup table for 'C' spot functions */

static SFLOOKUP sflookup[] = {
  {NAME_Round, round_sf},
  {NAME_Euclidean, euclidean_sf},
  {NAME_Elliptical1, elliptical_sf1},
  {NAME_Elliptical2, elliptical_sf2},
  {NAME_Line, line_sf},
  {NAME_Line90, line90_sf},
  {NAME_Square1, square_sf1},
  {NAME_Square2, square_sf2},
  {NAME_EllipticalQ1, ellipticalQ_sf1},
  {NAME_EllipticalQ2, ellipticalQ_sf2},
  {NAME_EllipticalP, ellipticalP_sf},
  {NAME_Rhomboid, rhomboid_sf},
  {NAME_InvertedSimpleDot, invertedsimpledot_sf},
  {NAME_Diamond, diamond_sf},
  {NAME_Ellipse, ellipse_sf},
  {NAME_EllipseA, ellipsea_sf},
  {NAME_InvertedEllipseA, invertedellipsea_sf},
  {NAME_EllipseB, ellipseb_sf},
  {NAME_InvertedEllipseB, invertedellipseb_sf},
  {NAME_EllipseB2, ellipseb_sf}, /* EllipseB2 maps to EllipseB */
  {NAME_InvertedEllipseB2, invertedellipseb_sf}, /* Similarly for InvertedEllipseB2 */
  {NAME_EllipseC, ellipsec_sf},
  {NAME_InvertedEllipseC, invertedellipsec_sf},
  {NAME_EllipseCAdobe, ellipsecadobe_sf},
  {NAME_LineAdobe, lineadobe_sf},
  {NAME_LineX, linex_sf},
  {NAME_LineY, liney_sf},
  {NAME_Square, square_sf},
  {NAME_Cross, cross_sf},
  {NAME_RhomboidAdobe, rhomboidadobe_sf},
  {NAME_DoubleDot, doubledot_sf},
  {NAME_InvertedDoubleDot, inverteddoubledot_sf},
  {NAME_CosineDot, cosinedot_sf},
  {NAME_Double, double_sf},
  {NAME_InvertedDouble, inverteddouble_sf},
  {NAME_EuclideanAdobe, euclidean_sf}
} ;


static Bool localnewhalftones(corecontext_t *context,
                              SYSTEMVALUE *usersfreqv ,
                              SYSTEMVALUE *usersanglv ,
                              OBJECT *proco ,
                              SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                              Bool accurate ,
                              Bool accurateInHalftoneDict ,
                              Bool doubleScreens ,
                              Bool requireActualAngleFreq ,
                              Bool *maybeAPatternScreen ,
                              NAMECACHE *htname ,
                              NAMECACHE *sfname ,
                              NAMECACHE *sfcolor ,
                              NAMECACHE *alternativeName,
                              NAMECACHE *alternativeColor,
                              HTTYPE cacheType,
                              Bool new_name ,
                              Bool overridefreq,
                              Bool overrideangle,
                              Bool complete,
                              int32 phasex ,
                              int32 phasey ,
                              OBJECT * poAdjustScreen,
                              GS_COLORinfo *colorInfo) ;

static Bool new_sf_name(corecontext_t *context, OBJECT *proco, NAMECACHE **name) ;


static Bool prepare_ch( corecontext_t *context,
                        CHALFTONE *tmp_chalftone,
                        SCELL_GEOM *sc_geom,
                        SPOTNO spotno,
                        HTTYPE type,
                        COLORANTINDEX color,
                        Bool *maybeAPatternScreen,
                        OBJECT *proco, SYSTEMVALUE iangle,
                        RESPI_PARAMS *respi_params,
                        int32 sfindex,
                        NAMECACHE *htname,
                        NAMECACHE * sfcolor ,
                        NAMECACHE *alternativeName,
                        NAMECACHE *alternativeColor,
                        HTTYPE cacheType,
                        NAMECACHE *underlying_spot_function_name,
                        int32 detail_name ,
                        int32 detail_index ,
                        Bool override_name ,
                        int32 phasex , int32 phasey,
                        uint8 default_bit_depth ) ;

static Bool adjustScreen(CHALFTONE * pchalftone, OBJECT * poAdjustScreen);


#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
static Bool screenNameIsHexHDS(NAMECACHE *htname)
{
  static uint8 *HexHDSPattern = (uint8*)"/Hds*";
  uint8 namestring[ LONGESTFILENAME ] ;

  /* far too long to cache on disk? */
  if (theINLen( htname ) > sizeof (namestring) - 2)
    return 0;

  swcopyf( namestring, (uint8 *)"/%.*s",
           theINLen(htname) ,
           theICList(htname)) ;

  return SwPatternMatch(HexHDSPattern, namestring);
}
#endif


static void freeSpotfns(register USERVALUE *uvals ,
                        register CELLS **cells ,
                        register int32 dots ,
                        Bool freeCellArray,
                        mm_addr_t *memAllocResult,
                        mm_size_t *memAllocSize )
{
  register int32 i , j ;

  if ( uvals )
    mm_free_with_header(mm_pool_temp,(mm_addr_t) uvals ) ;

  if ( cells ) {
    j = 0 ;
    for ( i = 0 ; i < dots ; ++i ) {
      HQASSERT( cells[ i ], "freeSpotfns: Should have allocated a cell for all in range" );
      if ( 0 < theIFreeCount( cells[ i ] ) )
        cells[ j++ ] = cells[ i ];
    }
    for ( i = 0; i < j ; i++ )
      mm_free(mm_pool_temp, (mm_addr_t)cells[ i ],
              theIFreeCount( cells[ i ] ) * sizeof( **cells ) );

    if ( freeCellArray )
      mm_free_with_header(mm_pool_temp, (mm_addr_t)cells ) ;
  }

  if ( memAllocResult ) {
    /* Then it had better be the one used below.  Let's make sure. */
    HQASSERT( sizeof( CHALFTONE ) == memAllocSize[ 2 ], "wrong memAlloc freed");
    mm_free(mm_pool_temp,memAllocResult[2],memAllocSize[2]);
  }
}


/* -------------------------------------------------------------------------- */
/* This routine looks for a procedure of the form:
 * a) { Fn --exec-- {} --exec-- }
 * b) { {} --exec-- Fn --exec-- }
 * and simply returns Fn.
 * Note that this applies recursively and exec MUST be an operator (names get
 * dealt with via AutomaticBinding, but at some stage we may need to also
 * do some lookup on "exec" to get "--exec--").
 * i.e. it simplifies proc if possible.
 * We have seen nested levels down to about 4500 levels, with something
 * like Euclidean at the bottom.
 */
static OBJECT *sf_simplify_array( OBJECT *proc )
{
  int32 fnDepth = 0 ;

  HQASSERT( proc != NULL , "proc NULL in sf_simplify_array" ) ;

  HQASSERT(oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY ,
            "proc not a procedure" ) ;

  while ((++fnDepth) < 0x0000FFFF ) { /* Prevent infinite recursion... */
    if ( theILen( proc ) == 4 ) {
      OBJECT *thea = oArray(*proc) ;
      HQASSERT( thea != NULL , "non-zero length arrays should not be NULL" ) ;
      if ( (oType(thea[0]) == OARRAY || oType(thea[0]) == OPACKEDARRAY) &&
           oExecutable(thea[0]) &&
           oType(thea[1]) == OOPERATOR &&
           oExecutable(thea[1]) &&
           theIOpName(oOp(thea[1])) == system_names + NAME_exec &&
           (oType(thea[2]) == OARRAY || oType(thea[2]) == OPACKEDARRAY) &&
           oExecutable(thea[2]) &&
           oType(thea[3]) == OOPERATOR &&
           oExecutable(thea[3]) &&
           theIOpName(oOp(thea[3])) == system_names + NAME_exec ) {
        if ( theILen( thea + 0 ) == 0 )
          proc = thea + 2 ;
        else if ( theILen( thea + 2 ) == 0 )
          proc = thea + 0 ;
        else
          break ;
      }
      else
        break ;
    }
    else
      break ;
  }
  return proc ;
}

/* -------------------------------------------------------------------------- */
/* Create a halftone entry for a spot function screen */
Bool newhalftones(corecontext_t *context,
                  SYSTEMVALUE *usersfreqv ,
                  SYSTEMVALUE *usersanglv ,
                  OBJECT *proco ,
                  SPOTNO spotno, HTTYPE httype, COLORANTINDEX color,
                  NAMECACHE *htname ,
                  NAMECACHE *sfcolor ,
                  NAMECACHE *alternativeName,
                  NAMECACHE *alternativeColor,
                  HTTYPE cacheType,
                  Bool accurate,
                  Bool accurateInHalftoneDict,
                  Bool doubleScreens,
                  Bool requireActualAngleFreq,
                  Bool *tellMeIfPatternScreen,
                  Bool docolordetection,
                  Bool overridefreq,
                  Bool overrideangle,
                  Bool complete,
                  int32 phasex ,
                  int32 phasey ,
                  OBJECT * poAdjustScreen,
                  GS_COLORinfo *colorInfo)
{
  int32 type ;
  Bool new_name ;
  Bool fEncapsulatedFn ;
  NAMECACHE *sfname ;

  /* Always use the /HalftoneName from the dictionary in preference to
   * that of the spotfunction name. e.g. an OEM dictionary screen called
   * Fred that is of Type5 who's subsidaries use Euclidean spot functions
   * will be called Fred and not Euclidean.
   */
  HQASSERT( proco , "proco null in newhalftone" ) ;

  new_name = FALSE ;
  fEncapsulatedFn = FALSE ;

  sfname = htname ;
  type = oType(*proco) ;
  if ( type != OFILE ) {
    /* A literal spot function name. */
    if ( type == ONAME && ! oExecutable(*proco) )
      sfname = oName(*proco) ;
    else {
      NAMECACHE *tmp_sf_name ;

      if ( type == OARRAY || type == OPACKEDARRAY ) {

        /* Harlequin extension: bind the spot function. */
        if ( context->userparams->AutomaticBinding ) {
          if ( ! bind_automatically(context, proco ))
            return FALSE ;
        }

        /* Check to see if we can simplify it. */
        proco = sf_simplify_array( proco ) ;
      }

      /* Check to see if the function is purely encapsulated. */
      fEncapsulatedFn = fn_PSCalculatorCompatible( proco , 16 ) ;

      /* PS object so parse switchscreens to look for the name. */
      if (( tmp_sf_name = findSpotFunctionName( proco )) != NULL )
        sfname = tmp_sf_name ;
    }
  }

  /* We need to check if a screen could be a pattern one. It obviously
   * can't be one if it is either a named one, or completely encapsulated.
   */
  if ( sfname == NULL ) {
    if ( ! fEncapsulatedFn ) {
      Bool maybeAPatternScreen = FALSE ;
      if ( ! localnewhalftones(context, usersfreqv , usersanglv , proco ,
                               spotno, httype, color,
                               accurate ,
                               accurateInHalftoneDict ,
                               doubleScreens ,
                               requireActualAngleFreq ,
                               & maybeAPatternScreen,
                               htname ,
                               sfname ,
                               sfcolor ,
                               alternativeName, alternativeColor, cacheType,
                               new_name ,
                               overridefreq ,
                               overrideangle ,
                               complete,
                               phasex , phasey ,
                               poAdjustScreen,
                               colorInfo))
        return FALSE ;

      if ( maybeAPatternScreen ) {
        if ( tellMeIfPatternScreen )
          (*tellMeIfPatternScreen) = TRUE ;
        return ( TRUE ) ;
      }
    }
    else if ( type != OFILE ) {
      /* We know that it is a real spot fn, so allow it to be cached. */
      new_name = new_sf_name(context, proco , & sfname ) ;
    }
  }

  if ( tellMeIfPatternScreen ) {
    (*tellMeIfPatternScreen) = FALSE ;
    return TRUE ;
  }

  if ( docolordetection ) {
    /* Note that this does the setcmykcolor trick as well as angle */
    if ( sfcolor == system_names + NAME_Gray ||
         sfcolor == system_names + NAME_Black ||
         sfcolor == system_names + NAME_Default )
      if ( ! detect_setscreen_separation( *usersfreqv , *usersanglv , colorInfo ))
        return FALSE ;
  }

  return localnewhalftones( context, usersfreqv , usersanglv , proco ,
                            spotno, httype, color,
                            accurate ,
                            accurateInHalftoneDict ,
                            doubleScreens ,
                            requireActualAngleFreq ,
                            NULL,
                            htname ,
                            sfname ,
                            sfcolor ,
                            alternativeName, alternativeColor, cacheType,
                            new_name ,
                            overridefreq ,
                            overrideangle ,
                            complete,
                            phasex , phasey ,
                            poAdjustScreen,
                            colorInfo ) ;
}

#define SYMETRICAL_START_VALUES
#undef  SYMETRICAL_START_VALUES
#if !defined( SYMETRICAL_START_VALUES )
/* AJCD: No idea what this function is for, but it returns the sequence
   0, -1, 0, 1, 4, 7, 12, 17, 24, 31, ...
   The -1 result for n == 1 appears to be an anomaly. I have no idea if
   this case can happen. */
static int32 offsetcoords( int32 n )
{
  int32 i , s ;

  HQASSERT(n >= 0, "offsetcoords() on negative value") ;

  if ( n == 0 )
    return ( 0 ) ;

  for ( i = 2 , s = -1 ; i <= n ; ++i )
    s += ( -1 + 2 * ( i / 2 )) ;

  return ( s ) ;
}
#endif

RESPI_WEIGHTS one_respi_weights =
{
  NULL,
  NULL,
  0
};

/* This routine returns the dot shape override that is to override the
 * setscreen, setcolorscreen or sethalftone call from PS. The override
 * started of life as a number into our list of spot funtions, then
 * became a ONAME which was the spot function to look up in switchscreens.
 * Finally, this "spot function" that was looked up was extended to be either
 * a spot function or a standard halftone dictionary which provides much more
 * control as well as allowing us to override spot function screens with
 * screens (and vica-versa).
 * What is returned includes the data to use as well as the name of the override.
 * At some stage the systemparam could be changed [added] to /DotShape.
 */
int32 getdotshapeoverride(corecontext_t *context,
                          NAMECACHE **ret_override_name , OBJECT **ret_override_data )
{
  SYSTEMPARAMS *systemparams = context->systemparams;

  HQASSERT( ret_override_name , "ret_override_name NULL in getdotshapeoverride" ) ;
  HQASSERT( ret_override_data , "ret_override_data NULL in getdotshapeoverride" ) ;

  (*ret_override_name) = NULL ;
  (*ret_override_data) = NULL ;

  /* New systemparam OverrideSpotFunctionName takes precedence over old one
   * OverrideSpotFunction.
   */
  if ( systemparams->OverrideSpotFunctionNameLen ) {
    NAMECACHE *override_name ;
    OBJECT    *override_data ;
    override_name = cachename( systemparams->OverrideSpotFunctionName ,
                               systemparams->OverrideSpotFunctionNameLen ) ;
    if ( NULL != (override_data = findSpotFunctionObject(override_name)) ) {
      /* Having got some PS object from switchscreens that corresponds to the
       * requested name check that it's either a dictionary or a spot function.
       */
      (*ret_override_name) = override_name ;
      (*ret_override_data) = override_data ;
      if (oType(*override_data) == OARRAY ||
          oType(*override_data) == OPACKEDARRAY )
        return SCREEN_OVERRIDE_SPOTFUNCTION ;
      else if ( oType(*override_data) == ODICTIONARY )
        return SCREEN_OVERRIDE_SETHALFTONE ;
      else {
        (*ret_override_name) = NULL ;
        (*ret_override_data) = NULL ;
        return SCREEN_OVERRIDE_NONE ;
      }
    }
  }
  else {
    if (( systemparams->OverrideSpotFunction >= 0 ) &&
        ( systemparams->OverrideSpotFunction < NUM_ARRAY_ITEMS( sflookup ))) {
      NAMECACHE *override_name ;
      OBJECT    *override_data ;
      override_name = system_names + sflookup[ systemparams->OverrideSpotFunction ].name ;
      if ( NULL != (override_data = findSpotFunctionObject(override_name)) ) {
        /* Having got some PS object from switchscreens that corresponds to the
         * requested name check that it's either a dictionary or a spot function.
         */
        (*ret_override_name) = override_name ;
        (*ret_override_data) = override_data ;
        if ( oType(*override_data) == OARRAY ||
             oType(*override_data) == OPACKEDARRAY )
          return SCREEN_OVERRIDE_SPOTFUNCTION ;
        else if ( oType(*override_data) == ODICTIONARY )
          return SCREEN_OVERRIDE_SETHALFTONE ;
        else {
          (*ret_override_name) = NULL ;
          (*ret_override_data) = NULL ;
          return SCREEN_OVERRIDE_NONE ;
        }
      }
    }
  }
  return SCREEN_OVERRIDE_NONE ;
}


static SYSTEMVALUE normalizeAngle(SYSTEMVALUE angle)
{
  SYSTEMVALUE rem = fmod(angle, 360.0);
  return rem < 0.0 ? rem + 360.0 : rem;
}


/* Check apparent_DOTS obeys GrayLevels (system param ScreenLimits) and
   fits in COLORVALUE; if not, adjust using supcell_ratio.

   dots is the actual number of dots or thresholds in the screen.
 */
static Bool limit_apparent_dots(int32 graylevels,
                                int32 *apparent_DOTS,
                                SYSTEMVALUE *supcell_ratio,
                                int32 dots, Bool limit_levels)
{
  Bool adjusted = FALSE;

  if ( limit_levels && *apparent_DOTS > graylevels - 1 ) {
    *supcell_ratio = (SYSTEMVALUE)dots / (graylevels - 1);
    *apparent_DOTS = (int32)((SYSTEMVALUE)dots / *supcell_ratio + 0.5);
    adjusted = TRUE;
  }
  if ( *apparent_DOTS > COLORVALUE_MAX ) {
    *supcell_ratio = (SYSTEMVALUE)dots / COLORVALUE_MAX;
    *apparent_DOTS = (int32)((SYSTEMVALUE)dots / *supcell_ratio + 0.5);
    adjusted = TRUE;
  }
  HQTRACE(debug_newhalftone && adjusted,
          ("adots: %d ratio %f", *apparent_DOTS, *supcell_ratio));
  return adjusted;
}


/*ARGSUSED*/
static Bool localnewhalftones(corecontext_t *context,
                              SYSTEMVALUE *usersfreqv ,
                              SYSTEMVALUE *usersanglv ,
                              OBJECT *proco ,
                              SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                              Bool accurate ,
                              Bool accurateInHalftoneDict ,
                              Bool doubleScreens ,
                              Bool requireActualAngleFreq ,
                              Bool *maybeAPatternScreen ,
                              NAMECACHE *htname ,
                              NAMECACHE *sfname ,
                              NAMECACHE *sfcolor ,
                              NAMECACHE *alternativeName,
                              NAMECACHE *alternativeColor,
                              HTTYPE cacheType,
                              Bool new_name ,
                              Bool overridefreq,
                              Bool overrideangle,
                              Bool complete,
                              int32 phasex , int32 phasey ,
                              OBJECT * poAdjustScreen,
                              GS_COLORinfo *colorInfo)
{
  DL_STATE *page = context->page ;
  SYSTEMPARAMS *systemparams = context->systemparams;
  SYSTEMVALUE stheta , ctheta , lxdpi, lydpi ;
  SYSTEMVALUE dt1, dt2, dt3, dt4 ;
  SYSTEMVALUE freqv0adjust = 0.0;

  CHALFTONE tmp_chalftone = { 0 }; /* init to zero, for cache stability */
  SCELL_GEOM sc_geom;

  SYSTEMVALUE ifreq , iangle , tangle ;

  int32 sfindex = none_sf ;
  Bool override_sf_name = FALSE ;

  int32 zeroadjust ; /* 0 => no, 1 => yes, from calculated frequency,
                        -1 => yes, from requested frequency */
  uint8 bit_depth_shift = (uint8)ht_bit_depth_shift(gsc_getRS(colorInfo) /* device RS */);
  int32 depth_factor = (1 << (1 << bit_depth_shift)) - 1;
  int32 effective_DOTS; /* DOTS * depth_factor */

  /* RESPI variables */
  RESPI_PARAMS respi_params;
  NAMECACHE *underlying_spot_function_name ;
  int32 detail_name = -1 ;
  int32 detail_index = 0 ;

  UNUSED_PARAM(Bool, accurateInHalftoneDict);
  HQTRACE(debug_newhalftone,("localnewhalftones: color=%d",color));

  tmp_chalftone.reportme = 0 ;
  tmp_chalftone.reportcolor = -1 ;
  theIAccurateScreen(&tmp_chalftone) = accurate ;
  tmp_chalftone.freqv = * usersfreqv ;
  tmp_chalftone.anglv = * usersanglv ;
  respi_params.respifying = FALSE;

#ifdef WE_NEED_A_THREE_WAY_SWITCH_HERE_SEE_ANDYC
  if ( ! accurateInHalftoneDict )
#endif
    theIAccurateScreen(&tmp_chalftone) = systemparams->AccurateScreens ;
  if ( doubleScreens )
    theIAccurateScreen(&tmp_chalftone) = FALSE ;

  NORMALISE_ANGLE(tmp_chalftone.anglv) ;

  if ( maybeAPatternScreen != NULL ) {    /* Check for patterns.             */
    if ( systemparams->PoorPattern ) /* Scale patterns down to 300 dpi. */
      tmp_chalftone.freqv *= ( page->xdpi / 300.0 ) ;
  }
  else {
    /* We don't do all this stuff if we're checking for a pattern screen. */
    if ( overridefreq &&
         systemparams->OverrideFrequency > 0.0 ) {
      tmp_chalftone.freqv = ( SYSTEMVALUE )systemparams->OverrideFrequency ;
      HQTRACE(debug_newhalftone,("override freq: %f",tmp_chalftone.freqv));
    }

    if ( overrideangle || rcbn_intercepting()) {
      /* Check if overriding the angle for this particular separation. If not
       * then fallback to using the old method for system parameter OverrideAngle.
       * First of all check for color interception.
       */
      SYSTEMVALUE overrideScreenAngle ;
      int32       fOverride ;

      COLORANTINDEX ci, ciOverride ;

      int32 nColorants ;
      DEVICESPACEID deviceSpace ;
      NAMECACHE *nmColorKey = sfcolor ;
      NAMECACHE *nmSepColor = get_separation_name(FALSE) ;
      guc_deviceColorSpace( gsc_getRS(colorInfo) , & deviceSpace , & nColorants ) ;

      if ( interceptSeparation(nmColorKey, nmSepColor, deviceSpace) )
        nmColorKey = nmSepColor;
      if (!guc_colorantIndexPossiblyNewName( gsc_getRS(colorInfo) , nmColorKey, &ci ))
        return FALSE;

      ciOverride = ci ;
      if ( rcbn_intercepting() && ci == rcbn_presep_screen(NULL) )
        /* Building the special presep screen, take the angle override
           from the ci of our best guess at the current separation */
        ciOverride = rcbn_likely_separation_colorant() ;

      if ( ! guc_overrideScreenAngle( gsc_getRS(colorInfo) , ciOverride ,
                                      & overrideScreenAngle , & fOverride ))
        return FALSE ;

      /* Override the angle or,
         if there is no override and we are recombining, need to force
         the screen angle to default for process colorants */
      if ( ( overrideangle && fOverride ) ||
           ( rcbn_intercepting() && rcbn_use_default_screen_angle( ci )))
        tmp_chalftone.anglv = overrideScreenAngle ;
    }

    if (poAdjustScreen != NULL) {
      if (! adjustScreen (& tmp_chalftone, poAdjustScreen))
        return FALSE;
    }
  }

  if ( systemparams->ScreenRotate)  /* Rotate screen according to page.  */
    tmp_chalftone.anglv += gsc_getScreenRotate( colorInfo ) ;
  tmp_chalftone.anglv = normalizeAngle( tmp_chalftone.anglv );

  HQTRACE(debug_newhalftone,("maybeAPatternScreen=%x accurate=%d sfname=%s proco=",
                             maybeAPatternScreen,
                             theIAccurateScreen(&tmp_chalftone),
                             (sfname != NULL ? theICList(sfname) : (uint8*)"null")));
#if 0 /* This would go to a different stream on LE, and is rarely useful */
#if defined( DEBUG_BUILD )
  if ( debug_newhalftone ) {
    debug_print_object_indented(proco, NULL, "\n", NULL);
  }
#endif
#endif

  if (maybeAPatternScreen == NULL) {
    if ( theIAccurateScreen(&tmp_chalftone) ) { /* Only do this if accurate/precision screening is on. */
      SYSTEMVALUE angletemp ;

      if ( systemparams->ScreenAngleSnap) {      /* Snap angles to the nearest angle in the angle set */
        int32 nearest7dot5 ;

        nearest7dot5  = ( int32 )( 2.0 * tmp_chalftone.anglv + 7.5 ) ;
        nearest7dot5 -= ( nearest7dot5 % 15 ) ;
        if ( nearest7dot5 == 720 )
          nearest7dot5 = 0 ;

        tmp_chalftone.anglv = ( SYSTEMVALUE )nearest7dot5 ;
        tmp_chalftone.anglv *= 0.5 ;
        HQTRACE(debug_newhalftone,("snap angle: %f",tmp_chalftone.anglv));
      }

      angletemp = tmp_chalftone.anglv ;
      while ( angletemp >= 90.0 )       /* Move angle from 0.0 <= angle < 360.0 into 0.0 <= angle < 90.0 */
        angletemp -= 90.0 ;
      if ( fabs( angletemp - systemparams->ScreenAngles[ 2 ] ) < EPSILON ) {     /* Adjust the 0 degree screen frequency */
        freqv0adjust = systemparams->ScreenZeroAdjust ;
        HQTRACE(debug_newhalftone,("freqv0adjust angle: %f",tmp_chalftone.anglv));
      }
    }
    if (systemparams->OverrideSpotFunctionNameLen) { /* override name take precedence over number */
      NAMECACHE *override_name ;
      OBJECT    *override_data ;
      override_name = cachename( systemparams->OverrideSpotFunctionName ,
                                 systemparams->OverrideSpotFunctionNameLen ) ;
      /* We only override the spot function if the override is a spot function */
      if ( NULL != (override_data = findSpotFunctionObject(override_name)) &&
           (oType(*override_data) == OARRAY ||
            oType(*override_data) == OPACKEDARRAY )) {
        new_name = FALSE ;      /* So we try and look it up */
        override_sf_name = TRUE ;
        sfname = override_name ;
      }
    }
    else {
      if (( systemparams->OverrideSpotFunction >= 0 ) &&
          ( systemparams->OverrideSpotFunction < NUM_ARRAY_ITEMS( sflookup ))) {
        NAMECACHE *override_name ;
        OBJECT    *override_data ;
        /* We only override the spot function if the override is a spot function */
        override_name = system_names + sflookup[ systemparams->OverrideSpotFunction ].name ;
        /* We only override the spot function if the override is a spot function */
        if ( NULL != (override_data = findSpotFunctionObject(override_name)) &&
             (oType(*override_data) == OARRAY ||
              oType(*override_data) == OPACKEDARRAY )) {
          new_name = FALSE ;    /* So we try and look it up */
          sfname = override_name ;
          HQTRACE(debug_newhalftone,("Override spot"));
        }
      }
    }
  }

  /* We have found a spot function name. Check to see if it is
     one of the HSL library. The underlying_spot_function_name, only
     changes if we have found an HMS type spot function */

  underlying_spot_function_name =
    theISFName( &tmp_chalftone ) = sfname ;
  if ( theISFName( &tmp_chalftone ) ) {
    if (!parse_HSL_parameters(context, colorInfo,
                              &tmp_chalftone,
                              &respi_params,
                              &one_respi_weights,
                              sfcolor,
                              maybeAPatternScreen != NULL ,
                              &underlying_spot_function_name,
                              &detail_name ,
                              &detail_index ,
                              &freqv0adjust,
                              sflookup,
                              NUM_ARRAY_ITEMS(sflookup)))
      return FALSE;
  }

  /* detail_name can be one of NAME_HDS, NAME_HCS, or NAME_HMS or
     -1 if the spot function was not an hsl one */
  if ( detail_name == NAME_HDS )
    sfindex =  dispersed_sf ;
  else if ( detail_name == NAME_HMS )
    sfindex = micro_sf ;
  else if ( detail_name == NAME_HCS )
    sfindex = chain_sf ;

/* FIND MOST ACCURATE SUPERCELL SIZE */
/* INPUT SCREEN VALUES ARE NOW THE PAIR (freqv,anglv). */

  if ( theIAccurateScreen( &tmp_chalftone) &&
       ( maybeAPatternScreen == NULL )) {
    Bool is_opt = FALSE;

    zeroadjust = fabs(freqv0adjust) > EPSILON ?
      (systemparams->ScreenZeroFromRequest ? -1 : 1) : 0;
    theISuperCellMultipleSize( &tmp_chalftone ) =
      accurateCellMultiple(context, tmp_chalftone.freqv, tmp_chalftone.anglv,
                           & ifreq , & iangle ,
                           &tmp_chalftone.ofreq, &tmp_chalftone.oangle,
                           &tmp_chalftone.dfreq, &is_opt,
                           zeroadjust < 0, zeroadjust,
                           tmp_chalftone.sfname) ;

    HQTRACE(debug_newhalftone,("scms: %d",theISuperCellMultipleSize( &tmp_chalftone )));
    if ( theISuperCellMultipleSize(&tmp_chalftone) <= 0 ) /* Abort if inaccurate screen.          */
      return error_handler( VMERROR ) ;

    tmp_chalftone.efreq  = fabs( tmp_chalftone.dfreq - tmp_chalftone.ofreq  ) ;
    tmp_chalftone.eangle = fabs( tmp_chalftone.anglv - tmp_chalftone.oangle ) ;
    HalfSetAngleUnoptimized(&tmp_chalftone, !is_opt);

    if ( fabs (freqv0adjust) > EPSILON ) {
      tmp_chalftone.dfreq += freqv0adjust * tmp_chalftone.dfreq ;
      ifreq = tmp_chalftone.dfreq ;
      tmp_chalftone.ofreq = tmp_chalftone.dfreq ;
      if ( zeroadjust > 0 ) {
        theISuperCellMultipleSize( &tmp_chalftone ) = 1;
        HQTRACE(debug_newhalftone,("scms: %d",theISuperCellMultipleSize( &tmp_chalftone )));
      } else {
        tmp_chalftone.efreq  = 0.0 ;
        tmp_chalftone.eangle = 0.0 ;
      }
    }
  }
  else {
    if ( maybeAPatternScreen == NULL )
      if ( fabs (freqv0adjust) > EPSILON )
        tmp_chalftone.freqv += freqv0adjust * tmp_chalftone.freqv ;

    ifreq  = tmp_chalftone.freqv ;
    iangle = tmp_chalftone.anglv ;
    tmp_chalftone.ofreq  = tmp_chalftone.freqv ;
    tmp_chalftone.oangle = tmp_chalftone.anglv ;
    tmp_chalftone.dfreq  = tmp_chalftone.freqv ;
    tmp_chalftone.efreq = 0.0  ;
    tmp_chalftone.eangle = 0.0 ;

    theISuperCellMultipleSize( &tmp_chalftone ) = 1 ;
    HQTRACE(debug_newhalftone,("scms: %d",theISuperCellMultipleSize( &tmp_chalftone )));
  }

  if ( requireActualAngleFreq ) {
    * usersfreqv = tmp_chalftone.ofreq ;
    * usersanglv = tmp_chalftone.oangle ;
  }
  if (! complete)
    return TRUE;

  tangle = iangle * DEG_TO_RAD ;
  stheta = sin( tangle ) ;
  ctheta = cos( tangle ) ;
  ifreq /= ( SYSTEMVALUE )theISuperCellMultipleSize( &tmp_chalftone)  ;

  lxdpi = page->xdpi;
  lydpi = page->ydpi;

  if (detail_name == NAME_HDS)
    lydpi = lxdpi;

  if ( lxdpi <= lydpi ) {
    dt4 = ( lxdpi * stheta ) / ifreq ;
    SC_RINT( theIHalfR4( &tmp_chalftone ) , dt4 ) ;
    sc_geom.AR4 = abs( theIHalfR4( &tmp_chalftone ) ) ;

    if ( lxdpi == lydpi ) {
      theIHalfR2( &tmp_chalftone ) = theIHalfR4( &tmp_chalftone )  ;
      sc_geom.AR2 = sc_geom.AR4 ;
      dt2 = dt4 ;
    } else {
      dt2 = lydpi * theIHalfR4( &tmp_chalftone ) / lxdpi ;
      SC_RINT( theIHalfR2( &tmp_chalftone ) , dt2 ) ;
      sc_geom.AR2 = abs( theIHalfR2( &tmp_chalftone ) ) ;
    }

    dt1 = ( lxdpi * ctheta ) / ifreq ;
    SC_RINT( theIHalfR1( &tmp_chalftone ) , dt1 ) ;
    sc_geom.AR1 = abs( theIHalfR1( &tmp_chalftone ) ) ;

    if ( lxdpi == lydpi ) {
      theIHalfR3( &tmp_chalftone ) = theIHalfR1( &tmp_chalftone )  ;
      sc_geom.AR3 = sc_geom.AR1 ;
      dt3 = dt1 ;
    } else {
      dt3 = lydpi * theIHalfR1( &tmp_chalftone ) / lxdpi ;
      SC_RINT( theIHalfR3( &tmp_chalftone ) , dt3 ) ;
      sc_geom.AR3 = abs( theIHalfR3( &tmp_chalftone ) ) ;
    }
  } else {
    dt2 = ( lydpi * stheta ) / ifreq ;
    SC_RINT( theIHalfR2( &tmp_chalftone ) , dt2 ) ;
    sc_geom.AR2 = abs( theIHalfR2( &tmp_chalftone ) ) ;

    dt4 = lxdpi * theIHalfR2( &tmp_chalftone ) / lydpi ;
    SC_RINT( theIHalfR4( &tmp_chalftone ) , dt4 ) ;
    sc_geom.AR4 = abs( theIHalfR4( &tmp_chalftone ) ) ;

    dt3 = ( lydpi * ctheta ) / ifreq ;
    SC_RINT( theIHalfR3( &tmp_chalftone ) , dt3 ) ;
    sc_geom.AR3 = abs( theIHalfR3( &tmp_chalftone ) ) ;

    dt1 = lxdpi * theIHalfR3( &tmp_chalftone ) / lydpi ;
    SC_RINT( theIHalfR1( &tmp_chalftone ) , dt1 ) ;
    sc_geom.AR1 = abs( theIHalfR1( &tmp_chalftone ) ) ;
  }

  if ( doubleScreens &&
       maybeAPatternScreen == NULL ) {
    dt1 *= 2.0 ; dt2 *= 2.0 ; dt3 *= 2.0 ; dt4 *= 2.0 ;
    theIHalfR1( &tmp_chalftone ) += theIHalfR1( &tmp_chalftone ) ; theIHalfR2( &tmp_chalftone ) += theIHalfR2( &tmp_chalftone ) ;
    theIHalfR3( &tmp_chalftone ) += theIHalfR3( &tmp_chalftone ) ; theIHalfR4( &tmp_chalftone ) += theIHalfR4( &tmp_chalftone ) ;
    sc_geom.AR1 += sc_geom.AR1 ; sc_geom.AR2 += sc_geom.AR2 ;
    sc_geom.AR3 += sc_geom.AR3 ; sc_geom.AR4 += sc_geom.AR4 ;
    theISuperCellMultipleSize( &tmp_chalftone )  += theISuperCellMultipleSize( &tmp_chalftone ) ;
  }

  if ( fabs( dt1 * dt3 ) + fabs( dt2 * dt4 ) > MAX_SCREEN_MEMORY )
    return error_handler( LIMITCHECK ) ;

  sc_geom.DOTS = (( sc_geom.AR1 * sc_geom.AR3 ) + ( sc_geom.AR2 * sc_geom.AR4 )) ;
  effective_DOTS = sc_geom.DOTS * depth_factor;
  HQTRACE(debug_newhalftone,("DOTS: %d R1 %d, R2 %d, R3 %d, R4 %d",sc_geom.DOTS,
                             theIHalfR1( &tmp_chalftone ),theIHalfR2( &tmp_chalftone ),
                             theIHalfR3( &tmp_chalftone ),theIHalfR4( &tmp_chalftone )));

#ifdef NO_DEGENERATES
  if ( sc_geom.DOTS < 2 ) /* Implies degenerate screen. */
        return TRUE ;
#else
  if ( sc_geom.DOTS == 0 ) {
    sc_geom.DOTS = 1; effective_DOTS = depth_factor;
    sc_geom.AR1 = sc_geom.AR3 = theIHalfR1( &tmp_chalftone ) = theIHalfR3( &tmp_chalftone ) = 1 ;
    sc_geom.AR2 = sc_geom.AR4 = theIHalfR2( &tmp_chalftone ) = theIHalfR4( &tmp_chalftone ) = 0 ;
  }
#endif

  theISuperCellRatio( &tmp_chalftone ) = 1.0 ;
  theISuperCellRemainder( &tmp_chalftone ) = 0 ;
  if ( theIAccurateScreen( &tmp_chalftone ) && maybeAPatternScreen == NULL
       && theISuperCellMultipleSize( &tmp_chalftone ) > 1) {
    theISuperCellRatio( &tmp_chalftone ) =
      theISuperCellMultipleSize( &tmp_chalftone ) * theISuperCellMultipleSize( &tmp_chalftone );
    theISuperCellRemainder( &tmp_chalftone ) =
      (int32)theISuperCellRatio( &tmp_chalftone ) - sc_geom.DOTS % (int32)theISuperCellRatio( &tmp_chalftone );
  }
  if ( maybeAPatternScreen == NULL && theIAccurateScreen( &tmp_chalftone )
       && systemparams->ScreenExtraGrays ) {
    if ( effective_DOTS < systemparams->ScreenLevels - 1 ) {
      /* Find a multiplier that makes effective_DOTS >= ScreenLevels - 1 */
      int32 inc = (int32)ceil(sqrt((systemparams->ScreenLevels - 1)
                                   / (double)effective_DOTS));

      theIHalfR1( &tmp_chalftone ) *= inc; theIHalfR2( &tmp_chalftone ) *= inc;
      theIHalfR3( &tmp_chalftone ) *= inc; theIHalfR4( &tmp_chalftone ) *= inc;
      sc_geom.AR1 *= inc; sc_geom.AR2 *= inc;
      sc_geom.AR3 *= inc; sc_geom.AR4 *= inc;
      tmp_chalftone.supcell_multiplesize *= inc;
      HQTRACE(debug_newhalftone, ("scms: %d", theISuperCellMultipleSize( &tmp_chalftone )));
      sc_geom.DOTS *= inc * inc; effective_DOTS *= inc * inc;
      HQASSERT(effective_DOTS >= systemparams->ScreenLevels - 1,
               "Not enough extragrays");
      HQTRACE(debug_newhalftone,
              ("fdots: %d r1 %d, r2 %d, r3 %d, r4 %d", sc_geom.DOTS,
               theIHalfR1( &tmp_chalftone ), theIHalfR2( &tmp_chalftone ),
               theIHalfR3( &tmp_chalftone ), theIHalfR4( &tmp_chalftone )));
      HQASSERT(systemparams->GrayLevels <= systemparams->ScreenLevels,
               "Gray level limits in the wrong order");
    }
    theISuperCellRatio( &tmp_chalftone ) =
      (SYSTEMVALUE)effective_DOTS / (systemparams->GrayLevels - 1);
    theISuperCellRemainder( &tmp_chalftone ) = 0;
  } else if ( detail_name == NAME_HDS ) {
    /* apply GrayLevels to HDS screens */
    if ( effective_DOTS > systemparams->GrayLevels - 1 ) {
      theISuperCellRatio(&tmp_chalftone) =
        (SYSTEMVALUE)effective_DOTS / (systemparams->GrayLevels - 1);
      theISuperCellRemainder(&tmp_chalftone) = 0;
    }
  }

  sc_geom.apparent_DOTS = effective_DOTS;
  if ( theISuperCellRatio( &tmp_chalftone ) > 1.0 ) {
    if (theISuperCellRemainder( &tmp_chalftone ) == 0)
      sc_geom.apparent_DOTS = (int32)(effective_DOTS / theISuperCellRatio( &tmp_chalftone ) + .5);
    else
      sc_geom.apparent_DOTS = effective_DOTS / (int32)theISuperCellRatio( &tmp_chalftone ) + 1;
  }
  if ( limit_apparent_dots(systemparams->GrayLevels, &sc_geom.apparent_DOTS,
                           &tmp_chalftone.supcell_ratio,
                           effective_DOTS, TRUE) )
    tmp_chalftone.supcell_remainder = 0;

  tmp_chalftone.notones = (uint16)sc_geom.apparent_DOTS;
  HalfSetMultithreshold(&tmp_chalftone, FALSE);
  HalfSetExtraGrays(&tmp_chalftone, FALSE);
  tmp_chalftone.hpstwo = (int8)((tmp_chalftone.accurateScreen
                                 && tmp_chalftone.supcell_multiplesize > 1)
                                && systemparams->HPSTwo);
  tmp_chalftone.supcell_actual = sc_geom.DOTS;
  tmp_chalftone.screenprotection = SCREENPROT_NONE;
  tmp_chalftone.maxthxfer = 0; /* not a threshold screen */

  if ( (sfname != NULL || alternativeName != NULL) && !new_name ) {
    int result;
    /* This is implicitly getting the original spot function rather than
       the underlying one... remember we want the underlying one only
       for the code that calculates the xy sample. */
    if (( result =
          ht_equivalent_ch_pre_cacheentry( context, spotno, type, color,
                                           &tmp_chalftone,
                                           0 /* depth_shift */,
                                           bit_depth_shift,
                                           0.0 /* angle */,
                                           htname, sfcolor,
                                           alternativeName,
                                           alternativeColor,
                                           cacheType,
                                           detail_name,
                                           detail_index,
                                           phasex, phasey )) != 0)
      return result > 0;
  }
  return prepare_ch( context, &tmp_chalftone, &sc_geom, spotno, type, color,
                     maybeAPatternScreen, proco, iangle,
                     &respi_params,
                     sfindex, htname, sfcolor,
                     alternativeName, alternativeColor, cacheType,
                     underlying_spot_function_name,
                     detail_name ,
                     detail_index ,
                     override_sf_name ,
                     phasex , phasey,
                     bit_depth_shift );
}


static Bool prepare_ch( corecontext_t *context,
                        CHALFTONE *tmp_chalftone,
                        SCELL_GEOM *sc_geom,
                        SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                        Bool *maybeAPatternScreen,
                        OBJECT *proco, SYSTEMVALUE iangle,
                        RESPI_PARAMS *respi_params,
                        int32 sfindex,
                        NAMECACHE *htname,
                        NAMECACHE *sfcolor,
                        NAMECACHE *alternativeName,
                        NAMECACHE *alternativeColor,
                        HTTYPE cacheType,
                        NAMECACHE *underlying_spot_function_name,
                        int32 detail_name ,
                        int32 detail_index ,
                        Bool override_name ,
                        int32 phasex , int32 phasey,
                        uint8 default_bit_depth )

{
  int32 INN , SIN , OUT , INDEX;
  int32 X   , Y   ;
  int32 SX  , SY  ;
  int32 noBlackDots, noWhiteDots;
  int32 I ;
  int32 patterngraylevel;
  int32 ISX , ISY ;
  int32 TSX , TSY ;
  int32 SCMS ;
  SYSTEMVALUE ax , ay ;
  SYSTEMVALUE tsx , tsy ;
  SYSTEMVALUE one = 1.0 ;
  int32 DOTS2 ;
  SYSTEMVALUE FDOTS , FDOTS2 ;
  SYSTEMVALUE PI_FDOTS ;
  SYSTEMVALUE TWICE_PI_FDOTS ;
  int32 itmp , itmp1 , itmp2 ;
  int32 cellx , celly ;
  USERVALUE fcellx ;
  USERVALUE fcelly ;
  USERVALUE * UVALS ;
  CELLS **tCELLS ;
  CELLS  *acell  ;
  SYSTEMVALUE arg = 0.0;
  SYSTEMVALUE tangle ;

  mm_size_t memAllocSize[ 3 ];
  mm_alloc_class_t memAllocClass[ 3 ] = {
    /* these 2 will be WITH_HEADER because they are variable size */
    MM_ALLOC_CLASS_HALFTONE_VALUES,
    MM_ALLOC_CLASS_HPS_CELL_TABLE,
    /* these without headers; they are fixed size */
    MM_ALLOC_CLASS_CHALFTONE,
  };
  mm_addr_t memAllocResult[ 3 ] ;
  CHALFTONE *newtone;

  /* We pass the original spot function to this, so that if it is a
     RESPI one, we will see it as that rather than as the underlying
     spot function (that has been respified)... remember the split is
     ORIGINAL (i.e., not hacked by HMS) vs. UNDERLYING (which might have
     been set by HMS).
   */
  if ( theIAccurateScreen( tmp_chalftone ) &&
       maybeAPatternScreen == NULL )
    report_screen_start( context, htname ,
                         tmp_chalftone->sfname ,
                         tmp_chalftone->freqv ,
                         tmp_chalftone->anglv );

  if (! oExecutable(*proco) && oType(*proco) != ONAME ) {
    /* the value is constant across the whole loop if the given object isn't
       executable - pretty stupid spot function really! */
    if (! object_get_numeric(proco, &arg))
      return FALSE;
  }

  {
    int32 actionNumber = 0;
    mm_result_t multiAllocResult;

    /* these two allocated WITH_HEADER because they are variable size */
    memAllocSize[ 0 ] = (sc_geom->DOTS+1) * sizeof( USERVALUE ) ;
    memAllocSize[ 1 ] = (sc_geom->DOTS+1) * sizeof( CELLS * ) ;
    memAllocSize[ 2 ] = sizeof(CHALFTONE);

    do {
      multiAllocResult = mm_alloc_multi_hetero_with_headers(mm_pool_temp, 2,
                                                            memAllocSize,
                                                            memAllocClass,
                                                            memAllocResult);
      if (multiAllocResult == MM_SUCCESS) {
        multiAllocResult = mm_alloc_multi_hetero(mm_pool_temp, 1,
                                                 memAllocSize + 2,
                                                 memAllocClass + 2,
                                                 memAllocResult + 2);
        if (multiAllocResult != MM_SUCCESS) {
          mm_free_with_header(mm_pool_temp,memAllocResult[0]);
          mm_free_with_header(mm_pool_temp,memAllocResult[1]);
        }
      }

      if (multiAllocResult == MM_SUCCESS) {
        int32 n, i;
        CELLS *acell;
        int32 DOTS = sc_geom->DOTS;

        n = 0 ;
        acell = NULL;  /* init to keep compiler quiet */
        tCELLS = (CELLS**)memAllocResult[1];

        while ( n < DOTS ) {
          for ( i = DOTS - n ; i > 0 ; i /= 2 ) {
            if ( (acell = ( CELLS * )mm_alloc(mm_pool_temp,
                                              i * sizeof( CELLS ),
                                              MM_ALLOC_CLASS_HPS_CELL)) != NULL )
              break ; /* the try-ever-smaller sizes for loop */
          }
          if ( i == 0 ) {
            freeSpotfns( (USERVALUE *)memAllocResult[0],
                         (CELLS**)memAllocResult[1],
                         n,     /* as many as we have allocated so far */
                         TRUE,  /* DO FREE cell array */
                         memAllocResult, memAllocSize );
            multiAllocResult = MM_FAILURE; /* so that we notice if we drop right out */
            break; /* from the n < DOTS loop */
          }
          n += i ;
          theIFreeCount( acell ) = i ;
          (*tCELLS++) = acell++ ;
          while ((--i) > 0 ) {
            theIFreeCount( acell ) = 0 ;
            (*tCELLS++) = acell++ ;
          }
        }
        HQASSERT( n <= DOTS, "n overflow!" );
        if (n == DOTS) { /* and we had success in the other allocs */
          HQASSERT( MM_SUCCESS == multiAllocResult, "Should have success status here" );
          break; /* alloc/handleLowMemory loop, we are all done */
        }
      }
      HQTRACE( debug_lowmemory, ( "CALL(handleLowMemory): prepare_ch" )) ;
      actionNumber = handleLowMemory( actionNumber, TRY_MOST_METHODS, NULL ) ;
      if ( actionNumber < 0 ) /* error */
        return FALSE;
    } while ( actionNumber > 0 ) ;
    if ( multiAllocResult != MM_SUCCESS ) /* then the allocations failed */
      return error_handler( VMERROR );
  }

  UVALS  = ( USERVALUE * )memAllocResult[0];
  tCELLS = ( CELLS    ** )memAllocResult[1];
  newtone = (CHALFTONE *)memAllocResult[2];
  *newtone = *tmp_chalftone;

  SCMS   = theISuperCellMultipleSize(tmp_chalftone) ;
  DOTS2  = 2 * sc_geom->DOTS ;
  FDOTS  = 1.0 / (SYSTEMVALUE)sc_geom->DOTS ;
  FDOTS2 = FDOTS * FDOTS ;
  PI_FDOTS = PI * FDOTS ;
  TWICE_PI_FDOTS = 2 * PI_FDOTS ;

  OUT = gcd( sc_geom->AR2 , sc_geom->AR3 );
  SIN = sc_geom->DOTS / OUT ;
  --OUT ;
  INN = SIN - 1 ;

#if defined( SYMETRICAL_START_VALUES )
  SX =  theIHalfR3(tmp_chalftone) * ( 1 - ( sc_geom->AR1 + sc_geom->AR4 )) + theIHalfR4(tmp_chalftone) * ( 1 - ( sc_geom->AR2 + sc_geom->AR3 )) ;
  SY = -theIHalfR2(tmp_chalftone) * ( 1 - ( sc_geom->AR1 + sc_geom->AR4 )) + theIHalfR1(tmp_chalftone) * ( 1 - ( sc_geom->AR2 + sc_geom->AR3 )) ;
#else
  { int32 sAR1 , sAR2 , tmp ;

    sAR1 = sc_geom->AR1 ;
    sAR2 = sc_geom->AR2 ;
    /* Move ar1,ar2 into 1/8 quadrant (absolute rotation) */
    for ( tangle = iangle ; tangle >= 90.0 ; tangle -= 90.0 ) {
      tmp = sAR1 ; sAR1 = sAR2 ; sAR2 = tmp ;
    }
    if ( tangle > 45.0 ) {
      tmp = sAR1 ; sAR1 = sAR2 ; sAR2 = tmp ;
    }

    if ( sAR1 == sAR2 ) {
      SX = offsetcoords( sAR1 + 1 /* X */ ) ;
      if ( ! ( sAR1 & 1 ))
        SX += 1 ;
      SY = SX - sAR2 ;
    }
    else {
      SX = offsetcoords( sAR1   /* X */ ) + offsetcoords( sAR2 /* Y */ ) ;
      SY = SX + 2 * sAR2 ;
    }
    SX = -SX ;
    SY = -SY ;
    /* Move result back into original quadrant (proper rotation) */
    if ( tangle > 45.0 ) {
      tmp = SX ; SX = SY ; SY = tmp ;
    }
    for ( tangle = iangle ; tangle >= 90.0 ; tangle -= 90.0 ) {
      tmp = SX ; SX = SY ; SY = -tmp ;
    }
  }
#endif

  X = ( sc_geom->DOTS * ( sc_geom->AR1 + sc_geom->AR4 ) + ( theIHalfR1(tmp_chalftone) * SX - theIHalfR4(tmp_chalftone) * SY )) / ( 2 * sc_geom->DOTS ) ;
  Y = ( sc_geom->DOTS * ( sc_geom->AR2 + sc_geom->AR3 ) + ( theIHalfR2(tmp_chalftone) * SX + theIHalfR3(tmp_chalftone) * SY )) / ( 2 * sc_geom->DOTS ) ;

  /* point the underlying spot fn name at the C procedure if it exists */
  if ( underlying_spot_function_name ) { /* the spot function exists in the dictionary, check against list of C functions */
    for ( I = 0 ; I < NUM_ARRAY_ITEMS(sflookup) ; ++I ) {
      if (system_names+sflookup[I].name == underlying_spot_function_name) {
        sfindex = sflookup[I].sfindex;
        HQTRACE(debug_newhalftone,("using 'C' spot function"));
        break ;
      }
    }
    /* Now check for overrides by name that aren't known as C functions */
    if ( override_name &&
        sfindex == none_sf ) {
      OBJECT *tmpo = findSpotFunctionObject( underlying_spot_function_name ) ;
      if ( tmpo )
        proco = tmpo ;
    }

  }

  fcellx = fcelly = 0.0f; /* init to keep compiler quiet */

  noBlackDots = 0 ;
  noWhiteDots = 0 ;
  patterngraylevel = -1 ;

  /* Can't use the generation number to determine if the cached spot
   * function is valid since it is determined after the fn_evaluate
   * calls. Instead, invalidate the current cached function, forcing
   * the function to be loaded into the cache next call to fn_evaluate
   * with a default generation number instead.
   */
  if ( (oType(*proco) == OFILE &&
        isIRewindable(oFile(*proco))) ||
       oType(*proco) == ODICTIONARY ) {
    fn_invalidate( FN_SPOT_FUNCTION , 0 ) ;
    fn_lock( FN_SPOT_FUNCTION , 0 ) ;
  }

  for (;;) {                    /* for all dots in the screen cell --
                           -- the loop exits when INN and OUT both go negative */
    SwOftenUnsafe() ;

    if ( SX >= sc_geom->DOTS ) {
      SX -= ( 2 * sc_geom->DOTS ) ;
      X -= theIHalfR1(tmp_chalftone) ;
      Y -= theIHalfR2(tmp_chalftone) ;
    } else if ( SX < -sc_geom->DOTS ) {
      SX += ( 2 * sc_geom->DOTS ) ;
      X += theIHalfR1(tmp_chalftone) ;
      Y += theIHalfR2(tmp_chalftone) ;
    }

    if ( SY >= sc_geom->DOTS ) {
      SY -= ( 2 * sc_geom->DOTS ) ;
      X -= -theIHalfR4(tmp_chalftone) ;
      Y -=  theIHalfR3(tmp_chalftone) ;
    } else if ( SY < -sc_geom->DOTS ) {
      SY += ( 2 * sc_geom->DOTS ) ;
      X += -theIHalfR4(tmp_chalftone) ;
      Y +=  theIHalfR3(tmp_chalftone) ;
    }

    ISX = SX ;
    ISY = SY ;

    if ( SCMS > 1 ) {
      ISX += sc_geom->DOTS ;
      ISY += sc_geom->DOTS ;

      TSX = ISX ;
      TSY = ISY ;
      cellx = celly = 0 ;

      for ( I = SCMS - 1 ; I > 0 ; --I ) {
        ISX += TSX ;
        if ( ISX >= DOTS2 ) {
          ISX -= DOTS2 ;
          ++cellx ;
        }
        ISY += TSY ;
        if ( ISY >= DOTS2 ) {
          ISY -= DOTS2 ;
          ++celly ;
        }
      }

      fcellx = ( float )cellx + ( float )(( double )ISX / ( double )DOTS2 ) ;
      fcelly = ( float )celly + ( float )(( double )ISY / ( double )DOTS2 ) ;

      ISX -= sc_geom->DOTS ;
      ISY -= sc_geom->DOTS ;
    }

    /* adjust ISX and ISY for RESPI */
    if (respi_params->respifying)
      {
        struct coord_t { int32 g; SYSTEMVALUE c; };
        struct cell_t  { struct coord_t x, y; };
        struct cell_t rc, qc;
        int32 i;

#define GRIDIFY(_i,_s,_c)       { SYSTEMVALUE mg = (_i)*((SYSTEMVALUE)(_s)); (_c).g = ((int32)mg > (_s) ? (_s)-1 : (int32)mg); (_c).c = (mg-((SYSTEMVALUE)((_c).g)))*2.0-1.0; }
#define CVG(_x,_y,_s,_g)        { GRIDIFY(((_x)+1.0)/2.0, (_s), (_g).x); GRIDIFY(((_y)+1.0)/2.0, (_s), (_g).y); }
#define RINT(_x,_y)             { SYSTEMVALUE _t = (_y); _x = ((int32)((_t)<0 ? (_t)-0.5 : (_t)+0.5)); }

        CVG((SYSTEMVALUE)ISX*FDOTS, (SYSTEMVALUE)ISY*FDOTS, respi_params->subcell_factor, rc);
        CVG(rc.x.c, rc.y.c, 2, qc);
        i = rc.x.g + rc.y.g * respi_params->subcell_factor;
        respi_params->light_weight = one_respi_weights.centre_weights[i];
        respi_params->dark_weight  = one_respi_weights.corner_weights[i*4 + (qc.x.g + qc.y.g * 2)];

        /* some spot functions grow fro the corners, and the weights need swapping */
        if (!respi_params->grow_from_centre)
          {
            SYSTEMVALUE tmp = respi_params->light_weight;
            respi_params->light_weight    = respi_params->dark_weight;
            respi_params->dark_weight     = tmp;
          }
        RINT(ISX, rc.x.c/FDOTS);
        ISY = (int32)(rc.y.c/FDOTS);
      }


    /* do the spot function */
    switch ( sfindex ) {
    case dispersed_sf :

      /* These cases can only happen in normal use if the override
         has been set and the screen cache not located. However
         we use it in house so that we can generate the
         caches in the first place. Therefore it runs the ordinary spot
         function - which will be set up to be the original PS source
         for HDS with us, but in the field would be the dummy
         or another screen from somewhere else, so:

         drop through to case 0, unless it is obviously not the HDS
         original spot function, tested by having /H pop at the
         beginning. OK, this is slow, because it happens each time
         round, but it's only so we can generate caches - it's normally
         an error.
         */

      if ((oType(*proco) != OARRAY && oType(*proco) != OPACKEDARRAY) ||
          theILen(proco) < 1 ||
          oType(*oArray(*proco)) != ONAME ||
          oName(*oArray(*proco)) != system_names + NAME_H ) {
        freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                     memAllocResult, memAllocSize );
        if ( (oType(*proco) == OFILE &&
              isIRewindable(oFile(*proco))) ||
             oType(*proco) == ODICTIONARY )
          fn_unlock( FN_SPOT_FUNCTION , 0 ) ;
        return error_handler (UNDEFINEDRESULT);
      }


      /* DROP THROUGH */
    default:    /* BEWARE! */
    case none_sf :
      tsx = ( SYSTEMVALUE )ISX * FDOTS ;
      tsy = ( SYSTEMVALUE )ISY * FDOTS ;

      if ( oExecutable(*proco) || oType(*proco) == ODICTIONARY ) {
        if ( (oType(*proco) == OFILE &&
              isIRewindable(oFile(*proco))) ||
             oType(*proco) == ODICTIONARY ) {
          USERVALUE input[ 2 ] , tmparg ;

          input[ 0 ] = ( USERVALUE ) tsx ;
          input[ 1 ] = ( USERVALUE ) tsy ;

          if ( ! fn_evaluate( proco , input , & tmparg ,
                              FN_SPOT_FUNCTION , 0 ,
                              FN_GEN_DEFAULT , FN_GEN_NA ,
                              NULL )) {
            freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE ,
                         memAllocResult , memAllocSize ) ;
            fn_unlock( FN_SPOT_FUNCTION , 0 ) ;
            return FALSE ;
          }

          arg = ( SYSTEMVALUE )tmparg ;
        }
        else {
          if ( ! stack_push_real( tsx, &operandstack ) ||
               ! stack_push_real( tsy, &operandstack ) ||
               ! push( proco , & executionstack )) {
            freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE ,
                         memAllocResult , memAllocSize ) ;
            return FALSE ;
          }

          if ( ! interpreter( 1 , NULL )) {
            freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE ,
                         memAllocResult , memAllocSize ) ;
            return FALSE ;
          }

          if ( ! stack_get_numeric(&operandstack, &arg, 1) ) {
            freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE ,
                         memAllocResult , memAllocSize ) ;
            return FALSE ;
          }

          if ( arg < -1.0 || arg > 1.0 ) {
            freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                         memAllocResult, memAllocSize );
            return error_handler( RANGECHECK ) ;
          }

          pop( & operandstack ) ;
        }
      }

      if ( arg == 1.0 )
        ++noBlackDots ;
      else if ( arg == 0.0 )
        ++noWhiteDots ;
      else {
        if ( maybeAPatternScreen != NULL ) {
          freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                         memAllocResult, memAllocSize );
          if ( (oType(*proco) == OFILE &&
                isIRewindable(oFile(*proco))) ||
               oType(*proco) == ODICTIONARY )
            fn_unlock( FN_SPOT_FUNCTION , 0 ) ;
          return TRUE ;
        }
      }
      break ;

    case round_sf :
      tsx = ( SYSTEMVALUE )ISX ;  tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ;  tsy *= tsy ;
      arg = ( one - ( tsx + tsy ) * FDOTS2 ) ;
      break ;

    case euclidean_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      if (( ISX + ISY ) > sc_geom->DOTS ) {
        ISX -= sc_geom->DOTS ;
        ISY -= sc_geom->DOTS ;
        tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
        tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
        arg = (( tsx + tsy ) * FDOTS2 - one ) ;
      }
      else {
        tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
        tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
        arg = ( one - ( tsx + tsy ) * FDOTS2 ) ;
      }
      break ;

    case elliptical_sf1 :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      tsx = ( SYSTEMVALUE )ISX * FDOTS ;
      tsy = ( SYSTEMVALUE )ISY * FDOTS ;
      if (( ISX + ISY ) > sc_geom->DOTS ) {
        tsx -= one ;    tsx *= tsx ;
        tsy -= one ;    tsy *= tsy ;
        arg = (( 0.524377 * tsx + tsy ) - one ) ;
      }
      else {
        tsx *= tsx ; tsy *= tsy ;
        arg = ( one - ( 0.524377 * tsx + tsy )) ;
      }
      break ;

    case elliptical_sf2 :
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
      arg = ( one - ( 0.524377 * tsx + tsy ) * FDOTS2 ) ;
      break ;

    case line_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      ISX = sc_geom->DOTS - ISX ;
      arg = ( SYSTEMVALUE )ISX * FDOTS ;
      break ;

    case line90_sf :
      if ( ISY < 0 ) ISY = -ISY ;
      ISY = sc_geom->DOTS - ISY ;
      arg = ( SYSTEMVALUE )ISY * FDOTS ;
      break ;

    case square_sf1:
      tsx = ( SYSTEMVALUE )ISX ;
      tsy = ( SYSTEMVALUE )ISY ;
      ax = ( ISX < 0 ) ? -tsx : tsx ;
      ay = ( ISY < 0 ) ? -tsy : tsy ;
      arg = ( one - 0.33 * FDOTS * ( ay + ax + 0.00011 * tsy + 0.0001 * tsx )) ;
      break ;

    case square_sf2 :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      if ( ISY > ISX )
        arg = ( SYSTEMVALUE )ISX * FDOTS ;
      else
        arg = ( SYSTEMVALUE )ISY * FDOTS ;
      break ;

    case ellipticalQ_sf1 :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      tsx = ( SYSTEMVALUE )ISX * FDOTS ;
      tsy = ( SYSTEMVALUE )ISY * FDOTS ;
      if (( 1.16 * tsx + tsy ) > one ) {
        tsx -= one ;    tsx *= tsx ;
        tsy -= one ;    tsy *= tsy ;
        arg = ( one - ( 0.33333333 * tsx + tsy )) ;
      }
      else {
        tsx *= tsx ; tsy *= tsy ;
        arg = (( 0.33333333 * tsx + tsy ) - one ) ;
      }
      break ;

    case ellipticalQ_sf2 :
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
      arg = ( one - ( 0.4 * tsx + tsy ) * FDOTS2 ) ;
      break ;

    case ellipticalP_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      itmp2 = ( sc_geom->DOTS << 2 ) ;
      if (( itmp1 = ((( ISX + ISY ) << 2 ) - ISX )) < ( itmp2 - sc_geom->DOTS )) {
          tsx = ( SYSTEMVALUE )ISX * FDOTS ; tsx *= tsx ;
          tsy = ( SYSTEMVALUE )ISY * FDOTS ; tsy *= tsy ;
          arg = ( one - 0.25 * ( tsx + 1.77777777 * tsy )) ;
      }
      else {
        if ( itmp1 > itmp2 ) {
          ISX -= sc_geom->DOTS ;
          ISY -= sc_geom->DOTS ;
          tsx = ( SYSTEMVALUE )ISX * FDOTS ; tsx *= tsx ;
          tsy = ( SYSTEMVALUE )ISY * FDOTS ; tsy *= tsy ;
          arg = ( 0.25 * ( tsx + 1.77777777 * tsy ) - one ) ;
        }
        else {
          arg = 3.5 - ( SYSTEMVALUE )itmp1 * FDOTS ;
        }
      }
      break ;

    case rhomboid_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      tsx = ( SYSTEMVALUE )ISX * FDOTS ;
      tsy = ( SYSTEMVALUE )ISY * FDOTS ;
      arg = ( one - ( 0.93 * tsx + 1.07 * tsy ) ) ;
      break ;

    case invertedsimpledot_sf :
      tsx = ( SYSTEMVALUE )ISX ;  tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ;  tsy *= tsy ;
      arg = ( tsx + tsy ) * FDOTS2 - one ;
      break ;

    case diamond_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      itmp = ISX + ISY ;
      arg = ( SYSTEMVALUE )itmp * FDOTS ;
      tsx = ( SYSTEMVALUE )ISX ;
      tsy = ( SYSTEMVALUE )ISY ;
      if ( arg <= 0.75 ) {
        tsx *= tsx ; tsy *= tsy ;
        arg = one - ( tsx + tsy ) * FDOTS2 ;
      }
      else if ( arg <= 1.23 ) {
        tsx *= 0.85 ;
        arg = one - ( tsx + tsy ) * FDOTS ;
      }
      else {
        tsx -= ( SYSTEMVALUE )sc_geom->DOTS ; tsx *= tsx ;
        tsy -= ( SYSTEMVALUE )sc_geom->DOTS ; tsy *= tsy ;
        arg = ( tsx + tsy ) * FDOTS2 - one ;
      }
      break ;

    case ellipse_sf:
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      arg = ( 4.0 * ( SYSTEMVALUE )ISX + 3.0 * ( SYSTEMVALUE )ISY ) * FDOTS ;
      if ( arg < 3.0 ) {
        tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
        tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy * 1.77777777 ;
        arg = one - ( tsx + tsy ) * 0.25 * FDOTS2 ;
      }
      else if ( arg > 4.0 ) {
        ISX -= sc_geom->DOTS ;
        ISY -= sc_geom->DOTS ;
        tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
        tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy * 1.77777777 ;
        arg = ( tsx + tsy ) * 0.25 * FDOTS2 - one;
      }
      else {
        arg = 3.5 - arg ;
      }
      break ;

    case ellipsea_sf :
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= 0.9 * tsy ;
      arg = one - ( tsx + tsy ) * FDOTS2 ;
      break ;

    case invertedellipsea_sf :
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= 0.9 * tsy ;
      arg = ( tsx + tsy ) * FDOTS2 - one ;
      break ;

    case ellipseb_sf :
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= 0.625 * tsy ;
      arg = sqrt( tsx + tsy ) ;
      arg = one - arg * FDOTS ;
      break ;

    case invertedellipseb_sf :
      /* Described in PDF manual, not implemented by Acrobat Reader. */
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= 0.625 * tsy ;
      arg = sqrt( tsx + tsy ) ;
      arg = arg * FDOTS - one ;
      break ;

    case ellipsec_sf :
      /* EllipseC as *implied* by PDF manual. */
      tsx = ( SYSTEMVALUE )ISX ; tsx *= 0.9 * tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
      arg = one - ( tsx + tsy ) * FDOTS2 ;
      break ;

    case invertedellipsec_sf :
      /* Described in PDF manual, not implemented by Acrobat Reader. */
      tsx = ( SYSTEMVALUE )ISX ; tsx *= 0.9 * tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
      arg = ( tsx + tsy ) * FDOTS2 - one ;
      break ;

    case ellipsecadobe_sf :
      /* EllipseC as implemented by Acrobat Reader. */
      tsx = ( SYSTEMVALUE ) ISX ;
      tsy = ( SYSTEMVALUE ) ISY ;
      tsx = tsx * 4.0 ; tsy = tsy * 4.0 ;
      arg = ( SYSTEMVALUE )sc_geom->DOTS * ( SYSTEMVALUE )sc_geom->DOTS ;
      if ( tsx > ( SYSTEMVALUE )DOTS2 ) tsx = 2.0 * ( SYSTEMVALUE )DOTS2 - tsx ;
      if ( tsy > ( SYSTEMVALUE )DOTS2 ) tsy = 2.0 * ( SYSTEMVALUE )DOTS2 - tsy ;
      if ( tsx >= ( SYSTEMVALUE ) sc_geom->DOTS ) {
        tsx = ( SYSTEMVALUE )DOTS2 - tsx ; tsx *= tsx ; tsx -= arg ;
      }
      else
        tsx = arg - tsx * tsx ;
      if ( tsy >= ( SYSTEMVALUE ) sc_geom->DOTS ) {
        tsy = ( SYSTEMVALUE )DOTS2 - tsy ; tsy *= tsy ; tsy -= arg ;
      }
      else
        tsy = arg - tsy * tsy ;
      arg = ( tsx + tsy ) * -0.5 * FDOTS2 ;
      break ;

    case lineadobe_sf :
      if ( ISY < 0 ) ISY = -ISY ;
      arg = ( SYSTEMVALUE ) -ISY * FDOTS ;
      break ;

    case linex_sf :
      arg = ( SYSTEMVALUE )ISX * FDOTS ;
      break ;

    case liney_sf :
      arg = ( SYSTEMVALUE )ISY * FDOTS ;
      break ;

    case square_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      if ( ISX > ISY )
        arg = ( SYSTEMVALUE ) -ISX * FDOTS ;
      else
        arg = ( SYSTEMVALUE ) -ISY * FDOTS ;
      break ;

    case cross_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      if ( ISX < ISY )
        arg = ( SYSTEMVALUE ) -ISX * FDOTS ;
      else
        arg = ( SYSTEMVALUE ) -ISY * FDOTS ;
      break ;

    case rhomboidadobe_sf :
      if ( ISX < 0 ) ISX = -ISX ;
      if ( ISY < 0 ) ISY = -ISY ;
      tsx = ( SYSTEMVALUE )ISX ; tsx *= 0.45 ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= 0.5 ;
      arg = ( tsx + tsy ) * FDOTS ;
      break ;

    case doubledot_sf :
      tsx = ( SYSTEMVALUE )ISX * TWICE_PI_FDOTS ; tsx = sin( tsx ) ;
      tsy = ( SYSTEMVALUE )ISY * TWICE_PI_FDOTS ; tsy = sin( tsy ) ;
      arg = ( tsx + tsy ) * 0.5 ;
      break ;

    case inverteddoubledot_sf :
      tsx = ( SYSTEMVALUE )ISX * TWICE_PI_FDOTS ; tsx = sin( tsx ) ;
      tsy = ( SYSTEMVALUE )ISY * TWICE_PI_FDOTS ; tsy = sin( tsy ) ;
      arg = ( tsx + tsy ) * -0.5 ;
      break ;


    case cosinedot_sf :
      tsx = ( SYSTEMVALUE )ISX * PI_FDOTS ; tsx = cos( tsx ) ;
      tsy = ( SYSTEMVALUE )ISY * PI_FDOTS ; tsy = cos( tsy ) ;
      arg = ( tsx + tsy ) * 0.5 ;
      break ;

    case double_sf :
      tsx = ( SYSTEMVALUE )ISX * PI_FDOTS ; tsx = sin( tsx ) ;
      tsy = ( SYSTEMVALUE )ISY * TWICE_PI_FDOTS ; tsy = sin( tsy ) ;
      arg = ( tsx + tsy ) * 0.5 ;
      break ;

    case inverteddouble_sf :
      tsx = ( SYSTEMVALUE )ISX * PI_FDOTS ; tsx = sin( tsx ) ;
      tsy = ( SYSTEMVALUE )ISY * TWICE_PI_FDOTS ; tsy = sin( tsy ) ;
      arg = ( tsx + tsy ) * -0.5 ;
      break ;

      /* see above for HDS cases */

    case micro_sf:

      /* This one can never happen because the respi
         converts the spot function to the underlying one.
         (I suppose if someone put the number of the underlying
         function as respi itself it could get here, but it
         is still really a programming error; there is a
         test on the range of the underlying spot) */
      HQFAIL ("tried to execute respi spot function");

    case chain_sf:
      tsx = ( SYSTEMVALUE )ISX ; tsx *= tsx ;
      tsy = ( SYSTEMVALUE )ISY ; tsy *= tsy ;
      arg = ( one - ( 0.05 * tsx + tsy ) * FDOTS2 ) ;
      break ;

      /****************************************************************
       * If you add a spot function here, you should add its matching *
       * PostScript version in edpdpss.pss and its name to the table  *
       * called "screens" in halftone.c.                              *
       ****************************************************************/

    }

    /* adjust ISX and ISY for RESPI */
    if (respi_params->respifying)
      {
        /* some values need fudging to make the weights noticeable */
        if (respi_params->growth_rate_fudge > 0.0 && arg < respi_params->mid_range)
          arg = (arg<0.0 ? -1.0 : 1.0) * pow(fabs(arg), respi_params->growth_rate_fudge);

        /* now weight it according to the z value */
        if      (arg > respi_params->light_side)
          arg = arg * respi_params->gradient + respi_params->light_weight;
        else if (arg < respi_params->dark_side)
          arg = arg * respi_params->gradient + respi_params->dark_weight;
        else
          arg = arg * respi_params->gradient;
      }

    INDEX = sc_geom->DOTS - (( OUT * SIN ) + INN ) - 1 ;
    UVALS[ INDEX ] = (USERVALUE) arg ;

    acell = tCELLS[ INDEX ] ;
    theIXCoord( acell ) = CAST_TO_INT16(X) ;
    theIYCoord( acell ) = CAST_TO_INT16(Y) ;

    if ( SCMS > 1 ) {
      int32 icellx , icelly ;

      while ( fcellx < 0.0 ) fcellx += (USERVALUE)SCMS ;
      while ( fcelly < 0.0 ) fcelly += (USERVALUE)SCMS ;

      icellx = ( int32 )( fcellx + 0.5 ) ;
      icelly = ( int32 )( fcelly + 0.5 ) ;
      while ( icellx >= SCMS ) icellx -= SCMS ;
      while ( icelly >= SCMS ) icelly -= SCMS ;
      theICellXY1( acell ) = icellx + icelly * SCMS ;

      icellx = ( int32 )( fcellx + 0.0 ) ;
      icelly = ( int32 )( fcelly + 0.0 ) ;
      while ( icellx >= SCMS ) icellx -= SCMS ;
      while ( icelly >= SCMS ) icelly -= SCMS ;
      theICellXY2( acell ) = icellx + icelly * SCMS ;
    }

    /* get ready for next time */
    --INN ;
    if ( INN < 0 ) {
      INN = SIN - 1 ;
      --OUT ;
      if ( OUT < 0 )
        break;

      SX += 2 * theIHalfR4(tmp_chalftone) ;
      SY += 2 * theIHalfR1(tmp_chalftone) ;
      ++Y ;
    }

    SX += 2 *  theIHalfR3(tmp_chalftone) ;
    SY += 2 * -theIHalfR2(tmp_chalftone) ;
    ++X ;
  }

  if ( (oType(*proco) == OFILE &&
        isIRewindable(oFile(*proco))) ||
       oType(*proco) == ODICTIONARY )
    fn_unlock( FN_SPOT_FUNCTION , 0 ) ;

  if ( sc_geom->DOTS > 2 ) {
    if (( noBlackDots + noWhiteDots ) == sc_geom->DOTS ) {
      if (( noWhiteDots != 0 ) || ( noWhiteDots != sc_geom->DOTS )) {
        patterngraylevel = noWhiteDots ;
        if ( maybeAPatternScreen )
          (*maybeAPatternScreen) = TRUE ;
      }
    }
  }
  else if ( maybeAPatternScreen ) {
    freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                         memAllocResult, memAllocSize );
    return TRUE ;
  }

  /* Now sort the halftones. */
  if ( sc_geom->DOTS > 1 )
    qsorthalftones( UVALS , tCELLS , sc_geom->DOTS ) ;
#if defined( ASSERT_BUILD )
  { int32 check ;
    for ( check = 1 ; check < sc_geom->DOTS ; ++check )
      HQASSERT( UVALS[ check - 1 ] <= UVALS[ check ] , "halftone not sorted" ) ;
  }
#endif
  if ( theIAccurateScreen(tmp_chalftone) &&
       theISuperCellMultipleSize(tmp_chalftone) > 1 ) {
    phasehalftones0( tCELLS , sc_geom->DOTS , UVALS ) ;
    if ( ! phasehalftones1( tCELLS , sc_geom->DOTS , theISuperCellMultipleSize(tmp_chalftone) )) {
      freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                         memAllocResult, memAllocSize );
      return FALSE ;
    }

    if ( context->systemparams->HPSTwo ) {
      if ( !spatiallyOrderHalftones(tCELLS, sc_geom->DOTS, newtone)) {
        freeSpotfns( UVALS , tCELLS , sc_geom->DOTS , TRUE,
                     memAllocResult, memAllocSize );
        return FALSE ;
      }
    } else
      if ( !phasehalftones2( tCELLS , sc_geom->DOTS ,
                             theISuperCellMultipleSize(tmp_chalftone) ))
        return FALSE ;
  }

  newtone->xcoords = (int16 *)UVALS;
  newtone->ycoords = newtone->xcoords + sc_geom->DOTS;
  for ( I = 0 ; I < sc_geom->DOTS ; ++I ) {
    newtone->xcoords[ I ] = theIXCoord( tCELLS[ I ] );
    newtone->ycoords[ I ] = theIYCoord( tCELLS[ I ] );
  }

  freeSpotfns( NULL, tCELLS, sc_geom->DOTS, TRUE, NULL, NULL );

  if (theIAccurateScreen(tmp_chalftone) &&
      maybeAPatternScreen == NULL )
    report_screen_end(context, htname,
                      tmp_chalftone->sfname,
                      tmp_chalftone->freqv,
                      tmp_chalftone->anglv,
                      tmp_chalftone->dfreq,
                      tmp_chalftone->efreq,
                      tmp_chalftone->eangle);

  newtone->halfxdims = sc_geom->AR1 + sc_geom->AR4;
  newtone->halfydims = sc_geom->AR2 + sc_geom->AR3;

  HQASSERT(sc_geom->apparent_DOTS >= 0, "Underflow in apparent_DOTS");
  newtone->notones = (uint16)(sc_geom->apparent_DOTS);

  /* Set up new halftone components, inserting into cache table. */
  return ht_insertchentry(context, spotno, type, color,
                          newtone,
                          0 /* default depth */,
                          default_bit_depth,
                          TRUE /* adjust */,
                          0.0 /* angle ignored */,
                          patterngraylevel,
                          htname, sfcolor,
                          detail_name, detail_index,
                          TRUE,
                          alternativeName, alternativeColor, cacheType,
                          phasex, phasey);
}


static uint8 *decrypt_newthreshold(corecontext_t *context,
                                   int32 encryptType, int32 encryptLength,
                                   int32 decryptLength, int32 depth,
                                   uint8 *buffer, int32 *pScreenProtection)
{
  HQASSERT( buffer, "encrypt_thresh is NULL in decrypt_newthreshold");
  HQASSERT( (encryptType < 3), "This decryption isn't yet implemented in decrypt_newthreshold");
  HQASSERT( pScreenProtection, "pScreenProtection is NULL in decrypt_newthreshold" );

  if ( encryptType == 1 ) { /* HQX */
    int32 result = 0;
    int32 keys[ 3 ];
    int32 secu[ 3 ];
    int32 i = 0;
    int32 offset;

    HQASSERT(encryptLength > 0, "encryptLength is 0 for a HQX encrypted threshold");

    keys[0] = HqxCryptSecurityNo();
    keys[1] = HqxCryptCustomerNo();
    keys[2] = 0x3b;  /* Generic Key */

    secu[0] = SCREENPROT_ENCRYPT_DONGLE;
    secu[1] = SCREENPROT_ENCRYPT_CUSTOMER;
    secu[2] = SCREENPROT_ENCRYPT_ANYHQN;

    do {
      *pScreenProtection= secu[i];
      result = hqx_check_leader_buffer( buffer, &offset, keys[ i++ ], NULL );
    } while ( result < 0 && i < 3 );

    if ( result )
      return NULL ;

    /* If there is too little data post-decryption, something is wrong. */
    if ( encryptLength - offset - hqxdataextra < decryptLength )
      return NULL ;

    /* If we have gotten this far, we are on the home stretch, the encryption key
     * has been found all that is left is to decode the buffer. We overshoot it a
     * little but it doesn't matter it's all a legit allocated buffer
     */

    hqx_crypt_region(buffer + offset, offset, encryptLength - offset, HQX_LEADERFLAG);

    /* The data we want to use is at this offset from the buffer */
    return buffer + offset ;
  }

  if ( encryptType == 2 ) { /* Harlequin-private-HQX for password-protected screens */
    int32 result = -1;  /* in case no features enabled */
    int32 features[ 4 ];
    int32 featkeys[ 4 ];
    int32 featsecu[ 4 ];
    int32 feat;
    int32 i;
    int32 offset = 0;
    int32 finalkey = 0;

    HQASSERT(encryptLength > 0, "encryptLength is 0 for a HQX encrypted threshold");

    /* To encourage low final key numbers, so we make the m.s. byte a value which
     * identifies the combination of the obfuscation method (currently only one)
     * and the password which must be enabled for the screen to be used.
     * The l.s. byte becomes the customer number.
     * We permit (but don't encourage) non-obfuscated variants so that hqcrypt can
     * be used to encrypt the tiles when testing, rather than a dedicated tool.
     */

    /* HDS, Un-obfuscated */
    features[0] = (context->systemparams->HDS == PARAMS_HDS_HIGHRES) ||
                    (context->systemparams->HDS == PARAMS_HDS_LOWRES);
    featkeys[0] = 0x0100;
    featsecu[0] = SCREENPROT_PASSREQ_HDS;

    /* HDS, Obfuscated */
    features[1] = features[0];
    featkeys[1] = 0x0200;
    featsecu[1] = featsecu[0];

    /* HXM, Un-obfuscated */
    features[2] = (context->systemparams->HXM == PARAMS_HXM_HIGHRES) ||
                    (context->systemparams->HXM == PARAMS_HXM_LOWRES);
    featkeys[2] = 0x0300;
    featsecu[2] = SCREENPROT_PASSREQ_HXM;

    /* HXM, Obfuscated */
    features[3] = features[2];
    featkeys[3] = 0x0400;
    featsecu[3] = featsecu[2];

    for ( feat = 0; ( result < 0 && feat < 4 ) ; feat++ ) {
      if ( features[feat] )
      { int32 keys[ 2 ];
        int32 secu[ 2 ];

        keys[0] = featkeys[feat] | (DongleCustomerNo() & 0xFF);
        keys[1] = featkeys[feat] | 0x3b;  /* Generic Key */

        secu[0] = (SCREENPROT_ENCRYPT_CUSTOMER | featsecu[feat]);
        secu[1] = (SCREENPROT_ENCRYPT_ANYHQN | featsecu[feat]);

        i= 0;
        do {
          finalkey = keys[i];
          *pScreenProtection = secu[i];
          result = hqx_check_leader_buffer( buffer, &offset, keys[ i++ ], NULL );
        } while ( result < 0 && i < 2 );
      }
    }

    if ( result )
      return NULL ;

    /* If there is too little data post-decryption, something is wrong. */
    if ( encryptLength - offset - hqxdataextra < decryptLength )
      return NULL ;

    if ( *pScreenProtection == SCREENPROT_PASSREQ_HXM && depth > 1 )
      return NULL; /* multi-bit HXM is not allowed */

    /* If we have gotten this far, we are on the home stretch, the encryption key
     * has been found all that is left is to decode the buffer. We overshoot it a
     * little but it doesn't matter it's all a legit allocated buffer
     */

    hqx_crypt_region(buffer + offset, offset, encryptLength - offset, HQX_LEADERFLAG);

    /* Undo any obfuscation */
    switch (finalkey & 0xFF00)
    { case 0x200:
      case 0x400:
        for (i = 0; i < (encryptLength - offset); i++)
        { buffer[offset + i]^= (uint8)finalkey;
          finalkey++;
        };
        break;
      default: break;
    }
    /* The data we want to use is at this offset from the buffer */
    return buffer + offset ;
  }

  return NULL;
}


/* Code common to the earlier part of threshold creation */
static Bool common_newthreshold_init(corecontext_t *context,
                                     OBJECT *stro,
                                     SPOTNO spotno, HTTYPE type, int32 color,
                                     NAMECACHE *htname,
                                     NAMECACHE *htcolor,
                                     NAMECACHE *alternativeName,
                                     NAMECACHE *alternativeColor,
                                     HTTYPE cacheType,
                                     Bool extragrays,
                                     Bool limitlevels,
                                     int32 DOTS,
                                     int32 R1, int32 R2, int32 R3, int32 R4,
                                     int32 depth,
                                     Bool type16,
                                     int32 encryptType,
                                     int32 encryptLength,
                                     GS_COLORinfo *colorInfo,
                                     /* output parameters: */
                                     CHALFTONE **tone_out,
                                     void **ZUVALS,
                                     FILELIST **flptr,
                                     uint8 **cryptbuf,
                                     Bool *htnamehit,
                                     OBJECT *newfileo)
{
  SYSTEMPARAMS *systemparams = context->systemparams;
  DL_STATE *page = context->page ;
  int32 detail_name = -1 ;
  int32 detail_index = 0 ;
  size_t thxfer_size = type16 ? MAXTHXSIZE + 1 : 256;
  int32 apparent_DOTS;
  Hq32x2 filepos;
  CHALFTONE template_tone = { 0 }; /* init to zero, for cache stability */
  SYSTEMVALUE angle = 0.0;
  uint8 bit_depth_shift = (uint8)ht_bit_depth_shift(gsc_getRS(colorInfo) /* device RS */);
  int32 depth_factor = (1 << (1 << bit_depth_shift)) - 1;

  template_tone.supcell_ratio = 1.0;
  template_tone.supcell_remainder = 0;
  template_tone.supcell_multiplesize = 1;
  template_tone.supcell_actual = DOTS;
  if ( extragrays ) {
    /* Map one colorvalue per dot.  The thresholds are only used to
       order the dots. */
    apparent_DOTS = DOTS;
    if ( depth == 1 ) /* 1-bit screens are scaled to current depth */
      apparent_DOTS *= depth_factor;
    /* If depth > 1, it's a multi-threshold, and DOTS is correct for it. */
    (void)limit_apparent_dots(systemparams->GrayLevels,
                              &apparent_DOTS, &template_tone.supcell_ratio,
                              apparent_DOTS, TRUE);
  } else { /* Map one colorvalue per threshold value, i.e., to thxfer array. */
    apparent_DOTS = (int32)(thxfer_size - 1);
    if ( depth == 1 ) /* 1-bit screens scaled to current depth before thxfer */
      apparent_DOTS *= depth_factor;
    /* Multi-threshold screens are scaled through thxfer, so take its size. */
    (void)limit_apparent_dots(systemparams->GrayLevels,
                              &apparent_DOTS, &template_tone.supcell_ratio,
                              apparent_DOTS, limitlevels);
  }
  HQASSERT(apparent_DOTS > 0, "Underflow in apparent_DOTS");

  template_tone.halfr1 = R1; template_tone.halfr2 = R2;
  template_tone.halfr3 = R3; template_tone.halfr4 = R4;
  template_tone.notones = (uint16)apparent_DOTS;
  HalfSetMultithreshold(&template_tone, depth != 1);
  template_tone.accurateScreen = FALSE;
  HalfSetExtraGrays(&template_tone, extragrays);
  template_tone.hpstwo = FALSE;
  template_tone.sfname = htname;
  template_tone.reportme = 0; template_tone.reportcolor = -1;
  template_tone.maxthxfer = (uint16)(thxfer_size - 1);

  if ( systemparams->ScreenRotate) { /* Rotate screen according to page */
    /* Normalize the angle so that further tests can use '=' */
    angle = normalizeAngle(gsc_getScreenRotate(colorInfo));
    if ( fabs(angle) < EPSILON )
      angle = 0.0;
    else if ( fabs(angle - 90.0) < EPSILON )
      angle = 90.0;
    else if ( fabs(angle - 180.0) < EPSILON )
      angle = 180.0;
    else if ( fabs(angle - 270.0) < EPSILON )
      angle = 270.0;
    /* Normalization can result in angle just < 360, so handle that */
    else if ( fabs(angle - 360.0) < EPSILON )
      angle = 0.0;
    else
      return detail_error_handler(RANGECHECK,
                                  "Threshold screens can only be rotated"
                                  "in multiples of 90°");
    if ( fabs(page->xdpi - page->ydpi) > EPSILON )
      return detail_error_handler(CONFIGURATIONERROR,
                                  "Can't rotate threshold screen anisopropically");
  }

  if ( htname != NULL || alternativeName != NULL ) {
    int32 result ;

    if ( oType(*stro) == OFILE &&
         oFile(*stro) == & std_files[ INVALIDFILE ] )
      detail_name = NAME_InvalidFile ;

    result = ht_equivalent_ch_pre_cacheentry(context, spotno, type, color,
                                             &template_tone,
                                             (uint8)gucr_ilog2(depth),
                                             bit_depth_shift,
                                             angle,
                                             htname, htcolor,
                                             alternativeName, alternativeColor,
                                             cacheType,
                                             detail_name,
                                             detail_index,
                                             gsc_getHalftonePhaseX(colorInfo),
                                             gsc_getHalftonePhaseY(colorInfo));
    if (result != 0) {
      *htnamehit = TRUE;
      return result > 0;
    }
  }

  /* Pass angle through to common_newthreshold_end() in oangle */
  template_tone.oangle = angle;

  if ( oType(*stro) != OSTRING ) {
    HQASSERT(oType(*stro) == OFILE,
              "threshold has to be string or file" );

    *flptr = oFile(*stro) ;
    HQASSERT( *flptr , "flptr NULL in newthresholds." ) ;
    if ( !isIOpenFileFilter( stro, *flptr ) ||
         !isIInputFile(*flptr) )
      return error_handler( IOERROR ) ;

    /* Layer an RSD filter if necessary.
     * Note: PDF adds its own RSD and sets the Threshold flag before
     * calling into PS [30573].
     */
    if ( !isIThreshold(*flptr) ) {
      Bool currentglobal;
      FILELIST *subfilt, *rsd ;
      Bool result ;

      /* Create file objects with same alloc mode to ensure validity */
      currentglobal = context->glallocmode || oGlobalValue(*stro);
      currentglobal = setglallocmode(context, currentglobal);

      /* First we need a SubFileDecode filter to give us the right number of
         bytes of data */
      subfilt = filter_standard_find(NAME_AND_LENGTH("SubFileDecode")) ;
      HQASSERT(subfilt != NULL, "Lost SubFileDecode") ;

      rsd = filter_standard_find(NAME_AND_LENGTH("ReusableStreamDecode")) ;
      HQASSERT(rsd != NULL, "Lost RSD") ;

      oInteger(inewobj) =
        encryptType == 0 ? (type16 ? 2*DOTS : DOTS) : encryptLength;
      theLen(snewobj) = 0;
      oString(snewobj) = NULL;
      result = (push3(stro, &inewobj, &snewobj, &operandstack) &&
                filter_create_object(subfilt, newfileo, NULL, &operandstack) &&
                push(newfileo, &operandstack) &&
                filter_create_object(rsd, newfileo, NULL, &operandstack)) ;

      /* Restore allocation mode */
      setglallocmode(context, currentglobal ) ;

      if ( !result )
        return FALSE ;

      HQASSERT(oType(*newfileo) == OFILE, "Filter layering did not yield a file") ;
      *flptr = oFile(*newfileo);
      SetIThresholdFlag(*flptr);
      rsdSetCircularFlag(*flptr, TRUE);
    }

    HQASSERT(isIRSDFilter(*flptr) && isIRewindable(*flptr) &&
             isIThreshold(*flptr),
             "common_newthreshold_init: unexpected file object");

    /* Now make sure we're at the start of the data */
    Hq32x2FromUint32(&filepos, 0u);
    if ( (*theIMyResetFile( *flptr ))( *flptr ) == EOF ||
         (*theIMySetFilePos( *flptr ))( *flptr , &filepos ) == EOF )
      return (*theIFileLastError(*flptr))(*flptr) ;
  }

  {
    static mm_alloc_class_t memAllocClass[4] = {
                              MM_ALLOC_CLASS_HALFTONE_VALUES,
                              MM_ALLOC_CLASS_HPS_CELL_TABLE,
                              MM_ALLOC_CLASS_TRANSFER_ARRAY,
                              MM_ALLOC_CLASS_EXTERN };
    mm_size_t memAllocSize[4];
    mm_addr_t memAllocResult[4];
    CHALFTONE *newtone;
    int32 actionNumber = 0;

    memAllocSize[0] = DOTS * (type16 ? sizeof( uint16 ) : sizeof( uint8 ));
    memAllocSize[1] = DOTS * 2 * sizeof( int16 );
    memAllocSize[2] = thxfer_size * sizeof( uint32 );
    memAllocSize[3] = encryptLength;

    do {
      if ( (newtone = mm_alloc(mm_pool_temp, sizeof(CHALFTONE),
                               MM_ALLOC_CLASS_CHALFTONE))
           != NULL ) {
        if ( mm_alloc_multi_hetero_with_headers(mm_pool_temp,
                                                encryptType != 0 ? 4 : 3,
                                                memAllocSize,
                                                memAllocClass, memAllocResult)
             == MM_SUCCESS )
          break; /* success */
        else { /* free before low-mem actions */
          mm_free(mm_pool_temp, newtone, sizeof(CHALFTONE));
          newtone = NULL;
        }
      }
      HQTRACE( debug_lowmemory, ( "CALL(handleLowMemory): newthresholds" ));
      actionNumber = handleLowMemory( actionNumber, TRY_MOST_METHODS, NULL );
      if ( actionNumber < 0 ) /* error */
        return FALSE;
    } while ( actionNumber > 0 );
    if (newtone == NULL)
      return error_handler(VMERROR);

    *ZUVALS = memAllocResult[0];
    *newtone = template_tone;
    newtone->xcoords = memAllocResult[1];
    newtone->ycoords = newtone->xcoords + DOTS;
    newtone->thxfer = memAllocResult[2];
    HqMemZero((uint8 *)newtone->thxfer, (int)memAllocSize[2]);
    if ( encryptType != 0 )
      *cryptbuf = memAllocResult[3];
    *tone_out = newtone;
  }
  return TRUE;
}


/* Code common to the final part of threshold creation */
static Bool common_newthreshold_end(corecontext_t *context,
                                    void *ZUVALS,
                                    SPOTNO spotno,
                                    HTTYPE type,
                                    COLORANTINDEX color,
                                    CHALFTONE *newtone,
                                    int32 depth,
                                    NAMECACHE *htname,
                                    NAMECACHE *htcolor,
                                    NAMECACHE *alternativeName,
                                    NAMECACHE *alternativeColor,
                                    HTTYPE cacheType,
                                    Bool type16,
                                    GS_COLORinfo *colorInfo)
{
  size_t i;
  uint8 *UVALS8 = NULL;
  uint16 *UVALS16 = NULL;
  uint32 *THXFER = newtone->thxfer;
  size_t thxfer_size;
  int32 DOTS = newtone->supcell_actual;
  SYSTEMVALUE orientation = newtone->oangle;
  uint8 bit_depth_shift = (uint8)ht_bit_depth_shift(gsc_getRS(colorInfo) /* device RS */);

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  if ( installing_hexhds ) { /* global ripvar */
    if ( screenNameIsHexHDS(htname) ) {
      if ( context->systemparams->HDS == PARAMS_HDS_HIGHRES )
        newtone->screenprotection = (SCREENPROT_ENCRYPT_NONE | SCREENPROT_PASSREQ_HDS);
      else {
        HQFAIL("Can't save protected screen without HDS password");
        return FALSE;
      }
    }
    else {
      HQFAIL("Trying to save non-HexHDS screen with protection flag set");
      return FALSE;
    }
  }
#endif

  if ( type16 )
    thxfer_size = MAXTHXSIZE + 1 ;
  else
    thxfer_size = 256;

  /* 0's are treated as 1's, so start from 1. */
  for ( i = 1 ; i < thxfer_size ; ++i )
    THXFER[ i ] = THXFER[ i ] + THXFER[ i - 1 ];
  THXFER[ 0 ] = 0; /* And that leaves no 0's (hence 0.0 paints black) */

  /* Now sort the coordinates according to the threshold values. */
  if ( type16 ) {
    UVALS16 = ZUVALS;
    qsort16threshold(UVALS16, newtone->xcoords, newtone->ycoords,
                     DOTS, 0, MAXTHXSIZE);
    for ( i = 0 ; i < (size_t) (DOTS - 1) ; ++i )
      HQASSERT(UVALS16[i] <= UVALS16[i+1], "type16 thresholds not sorted");
  }
  else {
    UVALS8 = ZUVALS;
    qsortthreshold(UVALS8, newtone->xcoords, newtone->ycoords, DOTS, 0, 255);
    for ( i = 0 ; i < (size_t) (DOTS - 1) ; ++i )
      HQASSERT(UVALS8[i] <= UVALS8[i+1], "thresholds not sorted");
  }

  mm_free_with_header( mm_pool_temp , ( mm_addr_t )ZUVALS ) ;

  newtone->oangle = 0.0; /* reset field used to pass orientation here */
  return ht_insertchentry(context, spotno, type, color,
                          newtone,
                          (uint8)gucr_ilog2(depth),
                          bit_depth_shift,
                          TRUE, orientation,
                          -1 /* patterngraylevel */,
                          htname, htcolor,
                          0, 0, /* detail */
                          TRUE,
                          alternativeName, alternativeColor, cacheType,
                          gsc_getHalftonePhaseX(colorInfo),
                          gsc_getHalftonePhaseY(colorInfo));
}


/* Utility macro for new*thresholds() */
#define GET_CVAL(_cval) MACRO_START                                           \
  if ( filearg ) {                                                            \
    if (( (_cval) = Getc( flptr )) == EOF ) {                                 \
      if ( cryptbuf )                                                         \
        mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf );           \
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )OUVALS ) ;             \
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )XCOORDS ) ;            \
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )THXFER ) ;             \
      mm_free( mm_pool_temp, newtone, sizeof(CHALFTONE) ); \
      /* if it reports no error (it is probably a valid end-of-file)          \
         still report it as an error */                                       \
      if ((*theIFileLastError( flptr ))( flptr ))                             \
        return error_handler(IOERROR);                                        \
      return FALSE ;                                                          \
    }                                                                         \
  } else {                                                                    \
    (_cval) = (*clist++) ;                                                    \
  }                                                                           \
MACRO_END


/* For type 3 and 6 halftones */
/*ARGSUSED*/
Bool newthresholds(corecontext_t *context,
                   int32 w, int32 h, int32 depth,
                   OBJECT *stro, Bool invert,
                   SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                   NAMECACHE *htname ,
                   NAMECACHE *htcolor ,
                   NAMECACHE *alternativeName,
                   NAMECACHE *alternativeColor,
                   HTTYPE cacheType,
                   Bool complete ,
                   Bool extragrays ,
                   OBJECT *newfileo,
                   int32 encryptType,
                   int32 encryptLength,
                   GS_COLORinfo *colorInfo)
{
  int16 i, j;
  size_t t;
  CHALFTONE *newtone;
  uint8 *clist = NULL; /* init to keep compiler quiet */
  FILELIST *flptr = NULL;
  size_t depth_factor = (1 << depth) - 1;
  int32 DOTS = (int32)depth_factor * w * h;
  int32 R1  , R2  , R3  , R4  ;
  int16 * XCOORDS, * YCOORDS ;
  uint32 *THXFER;
  uint8 *UVALS;
  void *OUVALS ;
  uint8 *cryptbuf = NULL ;
  int32 screen_protection = SCREENPROT_NONE;
  int32 filearg ;
  int32 cval ;

  UNUSED_PARAM(Bool, complete);

  HQASSERT( htcolor , "htcolor NULL in newthresholds" ) ;

#if defined( DEBUG_BUILD )
  if ( debug_newthreshold ) {
    if ( htname )
      monitorf((uint8*)"newthresholds: %.*s",theINLen(htname),theICList(htname));
    if ( htcolor )
      monitorf((uint8*)" %.*s",theINLen(htcolor),theICList(htcolor));
    monitorf((uint8*)"\n");
  }
#endif

  if ( oType(*stro) == OSTRING ) {
    filearg = FALSE ;
    clist = oString(*stro) ;
    HQASSERT( DOTS == theLen(*stro), "string not the correct size" );
  }
  else {
    filearg = TRUE ;
    if ( ( SYSTEMVALUE )depth_factor * w * h > MAX_SCREEN_MEMORY )
      return error_handler( LIMITCHECK ) ;
  }

  R1 = w; R2 = 0; R3 = h; R4 = 0;

  {
    Bool result, htnamehit = FALSE;
    result = common_newthreshold_init(context, stro,
                                      spotno, type, color,
                                      htname, htcolor,
                                      alternativeName, alternativeColor,
                                      cacheType,
                                      extragrays, FALSE,
                                      DOTS, R1, R2, R3, R4, depth, FALSE,
                                      encryptType, encryptLength,
                                      colorInfo,
                                      &newtone, &OUVALS, &flptr, &cryptbuf,
                                      &htnamehit,
                                      newfileo);
    if ( !result || htnamehit )
      return result;
  }

  XCOORDS = newtone->xcoords; YCOORDS = newtone->ycoords;
  THXFER = newtone->thxfer;
  UVALS = (uint8*)OUVALS;

  if ( encryptType != 0 ) {
    int32 loop ;

    for ( loop = 0 ; loop < encryptLength ; ++loop ) {
      GET_CVAL(cval);
      cryptbuf[loop] = (uint8)cval;
    }

    if ( (clist = decrypt_newthreshold(context, encryptType, encryptLength, DOTS, depth,
                                       cryptbuf, &screen_protection))
         == NULL ) {
      mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf );
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )OUVALS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )XCOORDS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )THXFER ) ;
      mm_free( mm_pool_temp, newtone, sizeof(CHALFTONE) );
      return error_handler(INVALIDACCESS) ;
    }

    filearg = FALSE ;
  }

  for ( t = 0 ; t < depth_factor ; ++t ) { /* loop over threshold layers */
    for ( j = 0 ; j < h ; ++j ) {
      SwOftenSafe ();
      for ( i = 0 ; i < w ; ++i ) {
        (*XCOORDS++) = i; (*YCOORDS++) = j;
        GET_CVAL(cval);
        if ( invert )
          cval = 255 - cval;
        ++THXFER[ cval ];
        (*UVALS++) = ( uint8 )cval ;
      }
    }
  }

  if ( cryptbuf )
    mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf);

  newtone->halfxdims = w; newtone->halfydims = h;
  newtone->screenprotection = (int8)screen_protection;
  return common_newthreshold_end(context, OUVALS, spotno, type, color,
                                 newtone,
                                 depth,
                                 htname, htcolor,
                                 alternativeName, alternativeColor, cacheType,
                                 FALSE, colorInfo);
}


/* For type 10 halftones.  See Adobe supplement 2015 for details */
Bool newXYthresholds(corecontext_t * context,
                     int32 x, int32 y, int32 depth,
                     OBJECT *stro, Bool invert,
                     SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                     NAMECACHE *htname ,
                     NAMECACHE *htcolor ,
                     NAMECACHE *alternativeName,
                     NAMECACHE *alternativeColor,
                     HTTYPE cacheType,
                     Bool complete ,
                     Bool extragrays ,
                     OBJECT *newfileo,
                     int32 encryptType,
                     int32 encryptLength,
                     GS_COLORinfo *colorInfo)
{
  int16 I, J;
  size_t t;
  CHALFTONE *newtone;
  uint8 *clist = NULL;
  size_t depth_factor = (1 << depth) - 1;
  int32 DOTS = (int32)depth_factor * (x * x + y * y);
  int32 R1  , R2  , R3  , R4  ;
  int16 * XCOORDS, * YCOORDS ;
  uint32 * THXFER ;
  FILELIST *flptr  = NULL;
  uint8 *UVALS;
  void  *OUVALS ;
  uint8 *cryptbuf = NULL ;
  int32 screen_protection = SCREENPROT_NONE;
  int32 filearg ;
  int32 cval ;

  UNUSED_PARAM(Bool, complete);

#if defined( DEBUG_BUILD )
  if ( debug_newthreshold ) {
    if ( htname )
      monitorf((uint8*)"newXYthresholds: %.*s",
               theINLen(htname),theICList(htname));
    if ( htcolor )
      monitorf((uint8*)" %.*s",theINLen(htcolor),theICList(htcolor));
    monitorf((uint8*)"\n");
  }
#endif

  HQASSERT(x > 0 && y > 0, "newXYthr: x and y should be +ve");
  HQASSERT( htcolor , "htcolor NULL in newthresholds" ) ;

  if ( oType(*stro) == OSTRING ) {
    filearg = FALSE ;
    clist = oString(*stro) ;
    HQASSERT( DOTS == theLen(*stro), "string not the correct size" );
  }
  else {
    filearg = TRUE ;
    if ((SYSTEMVALUE)depth_factor * ((SYSTEMVALUE)x * x + y * y)
        > MAX_SCREEN_MEMORY )
      return error_handler( LIMITCHECK ) ;
  }

  R1 = R3 = y;
  R2 = R4 = x;

  {
    Bool result, htnamehit = FALSE;
    result = common_newthreshold_init(context, stro, spotno, type, color,
                                      htname, htcolor,
                                      alternativeName, alternativeColor,
                                      cacheType,
                                      extragrays, FALSE,
                                      DOTS, R1, R2, R3, R4, depth, FALSE,
                                      encryptType, encryptLength,
                                      colorInfo,
                                      &newtone, &OUVALS, &flptr, &cryptbuf,
                                      &htnamehit,
                                      newfileo);
    if ( !result || htnamehit )
      return result;
  }

  XCOORDS = newtone->xcoords; YCOORDS = newtone->ycoords;
  THXFER = newtone->thxfer;
  UVALS = (uint8*)OUVALS;

  if ( encryptType != 0 ) {
    int32 loop ;

    for ( loop = 0 ; loop < encryptLength ; ++loop ) {
      GET_CVAL(cval);
      cryptbuf[loop] = (uint8)cval;
    }

    if ( (clist = decrypt_newthreshold(context, encryptType, encryptLength, DOTS, depth,
                                       cryptbuf, &screen_protection))
         == NULL ) {
      mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf );
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )OUVALS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )XCOORDS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )THXFER ) ;
      mm_free( mm_pool_temp, newtone, sizeof(CHALFTONE) );
      return error_handler(INVALIDACCESS) ;
    }

    filearg = FALSE ;
  }

  for ( t = 0 ; t < depth_factor ; ++t ) { /* loop over threshold layers */

    /* Get the X square */
    for ( J = 0; J < x; ++J ) {
      SwOftenSafe();
      for ( I = 0; I < x; ++I ) {
        *XCOORDS++ = I; *YCOORDS++ = J;
        GET_CVAL(cval);
        if ( invert )
          cval = 255 - cval;
        ++THXFER[ cval ];
        (*UVALS++) = (uint8)cval;
      }
    }

    /* And now the Y square */
    for ( J = CAST_TO_INT16(x); J < CAST_TO_INT16(x + y); ++J ) {
      SwOftenSafe();
      for ( I = 0; I < y; ++I ) {
        *XCOORDS++ = I; *YCOORDS++ = J;
        GET_CVAL(cval);
        if ( invert )
          cval = 255 - cval;
        ++THXFER[ cval ];
        (*UVALS++) = (uint8)cval;
      }
    }

  } /* loop over threshold layers */

  if ( cryptbuf )
    mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf);

  newtone->halfxdims = newtone->halfydims = x + y;
  newtone->screenprotection = (int8)screen_protection;
  return common_newthreshold_end(context, OUVALS, spotno, type, color,
                                 newtone,
                                 depth,
                                 htname, htcolor,
                                 alternativeName, alternativeColor, cacheType,
                                 FALSE, colorInfo);
}



/* For type 16 halftones.  See Adobe supplement 3010 for details */
Bool new16thresholds(corecontext_t *context,
                     int32 w, int32 h, int32 depth,
                     OBJECT *stro, Bool invert,
                     SPOTNO spotno, HTTYPE type, COLORANTINDEX color,
                     NAMECACHE *htname ,
                     NAMECACHE *htcolor ,
                     NAMECACHE *alternativeName,
                     NAMECACHE *alternativeColor,
                     HTTYPE cacheType,
                     Bool complete ,
                     Bool extragrays ,
                     Bool limitlevels ,
                     OBJECT *newfileo,
                     int32 w2, int32 h2,
                     int32 encryptType,
                     int32 encryptLength,
                     GS_COLORinfo *colorInfo)
{
  int16 I , J;
  size_t t;
  CHALFTONE *newtone;
  uint8 *clist = NULL;
  size_t depth_factor = (1 << depth) - 1;
  int32 DOTS = (int32)depth_factor * (w * h + w2 * h2);
  int32 R1  , R2  , R3  , R4  ;
  int16 * XCOORDS, * YCOORDS ;
  uint32 * THXFER ;
  FILELIST *flptr  = NULL;
  void *OUVALS;
  uint16 *UVALS;
  uint8 *cryptbuf = NULL ;
  int32 screen_protection = SCREENPROT_NONE;
  int32 filearg ;
  int32 cval ;
  uint32 lowbyte;

  UNUSED_PARAM(Bool, complete);

#if defined( DEBUG_BUILD )
  if ( debug_newthreshold ) {
    if ( htname )
      monitorf((uint8*)"new16thresholds: %.*s",
               theINLen(htname),theICList(htname));
    if ( htcolor )
      monitorf((uint8*)" %.*s",theINLen(htcolor),theICList(htcolor));
    monitorf((uint8*)"\n");
  }
#endif

  HQASSERT(w > 0 && h > 0, "new16thr: w and h should be +ve");
  HQASSERT((w2 == 0 && h2 == 0) || (w2 > 0 && h2 > 0),
           "new16thr: w2 and h2 - should have both or neither");
  HQASSERT( htcolor , "htcolor NULL in new16thresholds" ) ;

  /* Spec says files only.  Leave in for development */
  if ( oType(*stro) == OSTRING ) {
    filearg = FALSE ;
    clist = oString(*stro) ;
    /* Note - if we leave string support in post development, this assert needs
       to have a 'proper' test added in dosetht_check_screen() */
    HQASSERT( DOTS * 2 == theILen( stro ), "string not the correct size" );
  }
  else {
    filearg = TRUE ;
    if ((SYSTEMVALUE)depth_factor * ((SYSTEMVALUE)w * h + w2 * h2) * 2.0
        > MAX_SCREEN_MEMORY )
      return error_handler( LIMITCHECK ) ;
  }

  if ( w2 != 0 ) {
    R1 = w2; R2 = h; R3 = h2; R4 = w;
  } else {
    R1 = w; R2 = 0; R3 = h; R4 = 0;
  }
  {
    Bool result, htnamehit = FALSE;
    result = common_newthreshold_init(context, stro, spotno, type, color,
                                      htname, htcolor,
                                      alternativeName, alternativeColor,
                                      cacheType,
                                      extragrays, limitlevels,
                                      DOTS, R1, R2, R3, R4, depth, TRUE,
                                      encryptType, encryptLength,
                                      colorInfo,
                                      &newtone, &OUVALS, &flptr, &cryptbuf,
                                      &htnamehit,
                                      newfileo);
    if ( !result || htnamehit )
      return result;
  }

  XCOORDS = newtone->xcoords; YCOORDS = newtone->ycoords;
  THXFER = newtone->thxfer;
  UVALS = (uint16*)OUVALS;

  if ( encryptType != 0 ) {
    int32 loop ;

    for ( loop = 0 ; loop < encryptLength ; ++loop ) {
      GET_CVAL(cval);
      cryptbuf[loop] = (uint8)cval;
    }

    if ( (clist = decrypt_newthreshold(context, encryptType, encryptLength, DOTS * 2,
                                       depth,
                                       cryptbuf, &screen_protection))
         == NULL ) {
      mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf );
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )OUVALS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )XCOORDS ) ;
      mm_free_with_header( mm_pool_temp , ( mm_addr_t )THXFER ) ;
      mm_free( mm_pool_temp, newtone, sizeof(CHALFTONE) );
      return error_handler(INVALIDACCESS) ;
    }

    filearg = FALSE ;
  }

  /* The incoming threshold value must be scaled to the range of values
     supported by the threshold screen. This is done by multiplying by
     MAXTHXSIZE and dividing by MAXT16LEVEL. Check this won't go out of
     range. */
  HQASSERT(MAXUINT32 / MAXT16LEVEL >= MAXTHXSIZE,
           "Conversion of thresholds to colourvalues will fail") ;

  for ( t = 0 ; t < depth_factor ; ++t ) { /* loop over threshold layers */

    for ( J = 0 ; J < h ; ++J ) {
      SwOftenSafe ();
      for ( I = 0 ; I < w ; ++I ) {
        uint32 scval ;

        (*XCOORDS++) = I; (*YCOORDS++) = J;
        GET_CVAL(cval);   /* Get the high byte */
        GET_CVAL(lowbyte);
        scval = (uint32)((cval << 8) | lowbyte) * MAXTHXSIZE / MAXT16LEVEL ;
        if ( invert )
          scval = MAXTHXSIZE - scval;
        ++THXFER[ scval ] ;
        (*UVALS++) = CAST_UNSIGNED_TO_UINT16(scval) ;
      }
    }

    for ( J = CAST_TO_INT16(h) ; J < CAST_TO_INT16(h + h2) ; ++J ) {
      SwOftenSafe ();
      for ( I = 0 ; I < w2 ; ++I ) {
        uint32 scval ;

        (*XCOORDS++) = I; (*YCOORDS++) = J;
        GET_CVAL(cval); /* get the high byte */
        GET_CVAL(lowbyte);
        scval = (uint32)((cval << 8) | lowbyte) * MAXTHXSIZE / MAXT16LEVEL ;
        if ( invert )
          scval = MAXTHXSIZE - scval;
        ++THXFER[ scval ] ;
        (*UVALS++) = CAST_UNSIGNED_TO_UINT16(scval) ;
      }
    }

  } /* loop over threshold layers */

  if ( cryptbuf )
    mm_free_with_header( mm_pool_temp, ( mm_addr_t )cryptbuf);

  newtone->halfxdims = w + w2; newtone->halfydims = h + h2;
  newtone->screenprotection = (int8)screen_protection;
  return common_newthreshold_end(context, OUVALS, spotno, type, color,
                                 newtone,
                                 depth,
                                 htname, htcolor,
                                 alternativeName, alternativeColor, cacheType,
                                 TRUE, colorInfo);
}


/* ----------------------- Modular screens -------------------------------- */

/** Set up a modular screen;
 */
Bool newModularHalftone(SPOTNO spotno ,
                        HTTYPE type,
                        COLORANTINDEX ci,
                        OBJECT    *modname ,
                        NAMECACHE *htname ,
                        NAMECACHE *htcolor ,
                        OBJECT    *htdict, /* Can be subordinate of type 5 */
                        GS_COLORinfo *colorInfo )
{
  MODHTONE_REF *mhtref = NULL ;
  LISTCHALFTONE *listch;

  HQASSERT( htcolor , "htcolor NULL in newModularHalftone" ) ;
  HQASSERT( htdict , "htdict NULL in newModularHalftone" ) ;
  HQASSERT( oType( *htdict ) == ODICTIONARY,
            "htdict not dictionary in newModularHalftone" ) ;

  /* Attempt to select the modular halftone. */
  if ( !htm_SelectHalftone( modname,
                            htcolor,
                            htdict,
                            colorInfo,
                            &mhtref ) )
    return FALSE ; /* error_handler already called by htm_SelectHalftone */
  HQASSERT( mhtref, "No mhtref set up by htm_SelectHalftone" ) ;

  listch = ht_listchAlloc( spotno, type, ci, NULL, htcolor, htname, mhtref,
                           gsc_getHalftonePhaseX( colorInfo ),
                           gsc_getHalftonePhaseY( colorInfo ));
  if ( listch == NULL ) {
    htm_ReleaseHalftoneRef(mhtref);
    return FALSE;
  }
  ht_cacheInsert(listch);
  return TRUE ;
}


/* ----------------------- Sundries --------------------------------------- */


/*
 * new_sf_name: create a new name for proco in the switchscreen dictionary
 */
static OBJECT *newsf = NULL ;
static Bool new_sf_name(corecontext_t *context, OBJECT *proco, NAMECACHE **name)
{
  OBJECT *printerdict ;
  OBJECT *theo ;
  Bool success ;

  *name = NULL ;

  if (!context->systemparams->CacheNewSpotFunctions) {
    return FALSE ;
  }

  if ( ! newsf ) {
    oName(nnewobj) = system_names + NAME_DollarPrinterdict ;
    if ( (printerdict = fast_sys_extract_hash( & nnewobj )) != NULL ) {
      newsf = fast_extract_hash_name( printerdict , NAME_newsf ) ;
    }
    if ( !newsf )
      return FALSE ; /* time to give up already! */
  }

  if (!push(proco, &operandstack)) {
    return FALSE ;
  }

  if (!push(newsf, &executionstack)) {
    pop(&operandstack) ;
    return FALSE ;
  }

  if (! interpreter (1, NULL)) {
    return FALSE;
  }

  if ( isEmpty( operandstack )) {
    return error_handler( STACKUNDERFLOW ) ;
  }

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  success = oBool(*theo) ;

  pop(&operandstack) ;

  if (!success) {
    return FALSE ;
  }

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != ONAME )
    return error_handler( TYPECHECK ) ;

  *name = oName(*theo) ;

  pop(&operandstack) ;

  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static Bool adjustScreen(CHALFTONE * pchalftone, OBJECT * poAdjustScreen)
{
  SYSTEMVALUE aValues[2];

  HQASSERT (pchalftone != NULL, "NULL pchalftone given to adjustScreen");
  HQASSERT (poAdjustScreen != NULL, "NULL poAdjustScreen given to adjustScreen");

  /* The poAdjustScreen procedure takes frequency and angle from the
     pchalftone structure and returns a (possibly) modified pair */

  if (! stack_push_real(pchalftone->freqv, & operandstack))
    return FALSE;

  if (! stack_push_real(pchalftone->anglv, & operandstack)) {
    pop (& operandstack);
    return FALSE;
  }

  if (! push (poAdjustScreen, & executionstack)) {
    npop (2, & operandstack);
    return FALSE;
  }

  if (! interpreter (1, NULL))
    return FALSE;

  if (! stack_get_numeric(&operandstack, aValues, 2) )
    return FALSE;

  npop (2, & operandstack);

  pchalftone->freqv = aValues[0];
  pchalftone->anglv = aValues[1];

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static OBJECT *switchscreens = NULL ;

int32 findCSpotFunction(OBJECT *spotfn)
{
  NAMECACHE *name = findSpotFunctionName(spotfn) ;

  if ( name != NULL ) {
    int32 i ;

    for (i = 0; i < NUM_ARRAY_ITEMS(sflookup); ++i) {
      if (system_names + sflookup[i].name == name)
        return i ;
    }
  }

  return -1 ;
}

/*
 * findSpotFunctionName : Returns a pointer to the spot functio name in
 *                        switchscreens if found, else NULL
 */
NAMECACHE *findSpotFunctionName(OBJECT *spotfn)
{
  OBJECT *printerdict ;
  OBJECT_MATCH sfms ;

  if ( ! switchscreens ) {
    oName(nnewobj) = system_names + NAME_DollarPrinterdict ;
    if ( (printerdict = fast_sys_extract_hash( &nnewobj) ) != NULL) {
      switchscreens = fast_extract_hash_name(printerdict, NAME_switchscreens) ;
    }
  }

  sfms.obj = spotfn ;
  sfms.key = NULL ;
  if ( switchscreens ) {        /* Just in case! */
    if ( oExecutable(*spotfn) ) {
       walk_dictionary(switchscreens, wd_match_obj, &sfms) ;
    }
  }

  return (sfms.key && oType(*sfms.key) == ONAME) ? oName(*sfms.key) : NULL ;
}

OBJECT *findSpotFunctionObject(NAMECACHE *name)
{
  OBJECT *printerdict ;

  if ( ! switchscreens ) {
    oName(nnewobj) = system_names + NAME_DollarPrinterdict ;
    if ( (printerdict = fast_sys_extract_hash( &nnewobj) ) != NULL ) {
      switchscreens = fast_extract_hash_name(printerdict, NAME_switchscreens) ;
    }
  }

  oName(nnewobj) = name ;
  return fast_extract_hash( switchscreens , & nnewobj ) ;
}


void init_C_globals_gu_misc(void)
{
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  debug_newhalftone = FALSE;
#endif

#if defined( DEBUG_BUILD )
  debug_newthreshold = FALSE ;
#endif
  newsf = NULL ;
  one_respi_weights.centre_weights = NULL ;
  one_respi_weights.corner_weights = NULL ;
  one_respi_weights.last_nweights = 0 ;
  switchscreens = NULL ;
}

/* Log stripped */
