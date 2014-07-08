/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:params.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API for PS configuration parameters.
 */

#ifndef __PARAMS_H__
#define __PARAMS_H__

#include "objects.h"
#include "coreparams.h"  /* module_params_t */

/* Use of these macros is deprecated.  Either use an available corecontext
 * pointer or explicitly set one up.
 */
#define SystemParams        (*get_core_context_interp()->systemparams)
#define UserParams          (*get_core_context_interp()->userparams)

/** QuadState - This structure provides for quad-state control variables,
which are used to interpret optional user-specified boolean flags.
The NAME_DefaultTrue/False values determin what boolean value to use when an
optional flag is omitted.
The NAME_ForceTrue/False values provide for override behavior regardless
of the user-specified flag.

For example, image dictionaries contain an optional /Interpolate boolean value.
The InterpolateAllImages user parameter is a quad-state value that allows image
interpolation to take place:
a) as specified in the job, where an absent flag is defaulted to false
   (NAME_DefaultFalse)
b) as specified in the job, where an absent flag is defaulted to true
   (NAME_DefaultTrue)
c) always (NAME_ForceTrue)
d) never (NAME_ForceFalse)
*/

typedef struct {
  int16 nameNumber;
} QuadState;

/** Set the quad-state using the passed name number, which must be one of
NAME_DefaultTrue, NAME_DefaultFalse, NAME_ForceTrue, NAME_ForceFalse. */
Bool quadStateSetFromName(QuadState* self, int16 nameNumber);

/** Apply the quad-state to the passed user-specified value, returning the
value that should be used. If 'userValue' is NULL, it is assumed that the
user specified no value (allowing a default state to be used). If the user
specified a value, only a force state will override it. */
Bool quadStateApply(QuadState* self, Bool* userValue);

/** Convenience method to apply a QuadState to a PostScript object (which must
be NULL or an OBOOLEAN). */
Bool quadStateApplyToObject(QuadState* self, OBJECT* userValue);

typedef struct {
  /* A constructed dict containing current values, for passing back to
   * currentuserparams */
  OBJECT paramDict;

  /* Is image decimation enabled */
  Bool enabled;

  /* The minimum area of a source image before it is considered for decimation. */
  uint32 minimumArea;

  /* The minimum percentage of device resolution an image must be before it is
   * considered for decimation. */
  uint32 minimumResolutionPercentage;
} DecimationParams;

/*
 * User parameters.
 *
 * Warning: This structure is included into the SAVELIST structure. Any
 * changes to this structure will likely require a complete rebuild.
 */

#define MAX_FORCEFONTCOMPRESS 10

typedef struct userparams {
  int32 MaxFontItem ;
  int32 MinFontCompress ;
  int32 MaxUPathItem ;
  int32 MaxFormItem ;
  int32 MaxPatternItem ;
  int32 MaxScreenItem ;
  int32 MaxOpStack ;
  int32 MaxDictStack ;
  int32 MaxExecStack ;
  int32 MaxLocalVM ;
  int32 VMReclaim ;
  int32 VMThreshold ;
  int32 ShowpageExtraMemory;

  int8  AutomaticBinding;
  int8  ImpositionForceErasePage;
  uint8 SnapImageCorners;

  /* Global control over image/mask interpolation (a quad-state name number). */
  QuadState InterpolateAllImages;
  QuadState InterpolateAllMasks;

  /* The user parameters that are also visible as variables in
     statusdict were in here, briefly, until we decied to keep
     just one copy of the information, in the statusdict variables.
  */
  int32 ForceFontCompress[ MAX_FORCEFONTCOMPRESS ] ;
  int8  NeverRender;

  int8  VignetteDetect;         /* bool for on/off for features */

  int8  AdobeFilePosition;      /* bool to indicate action on fileposition */
                                /* on %stdin */

  int32 VignetteMinFills;       /* min. fills to make a vignette */
  USERVALUE VignetteLinearMaxStep; /* max color change per step, abs. */
  USERVALUE VignetteLogMaxStep; /* max color change per step, percentage*/

  int8  IdiomRecognition ;
  int8  RejectPreseparatedJobs;

  USERVALUE  ResolutionFactor ;

  int8  HalftoneMode;
  int8  AccurateScreens ;       /* Dummy param since we already have equivalent systemparam */

  int8 AdobeArct ;              /* Adobe compatible arct(o) - single curves */
  QuadState ShadingAntiAliasDefault; /* Default value for /ShadingAntiAliasDefault in a shading dictionary */

  USERVALUE MinShadingSize ;    /* minimum size of smooth shading decomposition */
  int32 GouraudLinearity ;      /* Extra samples to test linearity */
  int32 MaxGouraudDecompose ;   /* Maximum depth of gourauds */
  int32 BlendLinearity ;        /* Extra samples to test linearity */

  int32 MaxSuperScreen ;

  int8 RecombineWeakMergeTest;  /* Allow trap matches between objects with complex clipping */
  USERVALUE RecombineTrapWidth; /* Constrains trap matches between objects (default value 0.144) */

  int32 RecombineObject;        /* Completely disable (0), Dynamic control (1), Chars only (2). */
  int32 RecombineObjectThreshold; /* Beyond threshold, dynamically decide whether to continue object merging. */
  USERVALUE RecombineObjectProportion; /* Continue object merging only if proportion of objects merged exceeds this number. */

  int32 BackdropReserveSize;    /* Reserve size after allocating backdrop blist resources */
  uint32 BackdropResourceLimit; /* Limit of max backdrop resource requirement, if possible. */
  int8 UseScreenCacheName;      /* Whether to send the actual screen cache name to screendev */

  int8 SilentFontFault;         /* Stop the whinging when a font isn't found */
  USERVALUE MinSmoothness, MaxSmoothness; /* Limits for shading smoothness */
  USERVALUE ShadingAntiAliasSize ;   /* Noise addition grid size */
  USERVALUE ShadingAntiAliasFactor ; /* Noise addition multiple */

  int8 PatternOverprintOverride; /* Overprint settings taken from the pattern'd object. */
  int8 EnablePseudoErasePage;    /* Whether or not to detect pseudo erase page. */
  uint8 StrokeScanConversion;  /* Scan converter to use for strokes. */
  uint8 CharScanConversion;    /* Scan converter to use for large characters. */
  uint8 PCLScanConversion;     /* Scan converter to use for PCL pixel edge. */

  int32 RetainedRasterCompressionLevel; /* How much effort to put into
                                           compressing retained raster
                                           backgrounds in memory: 0
                                           (no compression) to 9 (max
                                           effort) */
  DecimationParams decimation; /* Image decimation settings. */
  int8 ImageDownsampling;
  int8 OverridePatternTilingType;
  int8 AlternateJPEGImplementations; /* Allow alternate JPEG implementations, e.g. libjpeg */
} USERPARAMS ;

#define MAX_PASSWORD_LENGTH 30
#define MAX_SF_NAME_LENGTH  32
#define MAX_RESOURCE_DIR_LENGTH 256
#define MAX_DEVICE_NAME_LENGTH 53
#define PCP_ELEMENTS 9
#define SLS_ELEMENTS 2

/*
 * System parameters.

   Procedure for adding a new system parameter:
   1. Add PostScript name to bottom of names.nam
   2. Add to structure below
   3. Increment count of parameters below this structure
   4. Add entry to dictmatch table in params.c
   5. Add to setsystemparams after other readers in params.c following the
      same general pattern
   6. Add to currentsystemparams (usually) after other writers
      following the same general pattern
   7. Compile params.c, names.nam, dicthash.c and the files that use the
      parameter.
 */

typedef struct SYSTEMPARAMS {
  uint8  *SystemPassword ;
  int32 SystemPasswordLen ;
  uint8  *StartJobPassword ;
  int32 StartJobPasswordLen ;
  int32 BuildTime ;
  int8  *BuildDate ;
  int32 BuildDateLen ;
  int32 ByteOrder ;
  uint8  *RealFormat ;
  int32 RealFormatLen ;
  int32 MaxOutlineCache ;
  int32 CurOutlineCache ;
  int32 MaxUPathCache ;
  int32 CurUPathCache ;
  int32 MaxFormCache ;
  int32 CurFormCache ;
  int32 MaxPatternCache ;
  int32 CurPatternCache ;
  int32 MaxScreenStorage ;
  int32 CurScreenStorage ;
  int32 MaxDisplayList ;
  int32 CurDisplayList ;
  int32 MaxDisplayAndSourceList ;
  int32 MaxSourceList ;
  int32 CurSourceList ;
  /* Harlequin extensions to the system params */
  uint8 LanguageLevel ;   /* 1 or 2 */
  uint8 ImagemaskBug ;    /* if language level is 1, emulate imagemask bug */
  uint8 CompositeFonts ;  /* if language level is 1, allow composite fonts */
  uint8 ColorExtension ;  /* if language level is 1, allow some color ops  */

  uint8 ParseComments ;   /* whether or not to parse comments */

  uint8 AutoShowpage ;    /* whether or not to do a showpage for epsf */

  uint8 AutoPrepLoad ;    /* whether or not to */
  uint8 PoorPattern ;     /* turns off recognition of pattern screens */
  uint8 AccurateScreens ; /* accurate screens on or off if job doesn't say */
  uint8 ScreenRotate ;   /* with the job */

  uint8 ScreenWithinTolerance ;
  uint8 ScreenExtraGrays ;
  uint8 ScreenDotCentered ;
  uint8 ScreenAngleSnap ;

   int8 OverrideSpotFunction ;
   int8 CompressBands ;

  USERVALUE OverrideFrequency ;

  USERVALUE ScreenZeroAdjust ;
  USERVALUE ScreenAngleAccuracy ;
  USERVALUE ScreenFrequencyAccuracy ;
  USERVALUE ScreenFrequencyDeviation ;

  int32 MaxGsaves ;           /* upper limit on the number of gsaves:
                               * 0 means unlimited */
  int32 MaxInterpreterLevel ; /* upper limit on recursive invocations of PS
                               * interpreter; level > 0 && level =< 256
                               */

  uint8 CountLines ;      /* if TRUE count input PS lines */
   int8 PoorStrokepath ;
   int8 Level1ExpandDict ; /* if true expand dictionaries even in level
                              1 mode */
  uint8 CacheNewSpotFunctions ; /* cache new PostScript spot functions ? */

  USERVALUE PreAllocation ;  /* Number of halftone levels to ignore before
                              * pre-allocating a form for the current screen */
  int32 GrayLevels ;    /* limit the number of gray levels such that if the
                          screen naturally exceeded this number, it would be
                          reduced as close as reasonable to this number */
  int32 DynamicBands ; /* this says if we are doing dynamic bands */
  int32 DynamicBandLimit ; /* this gives the maximim number of dynamic bands
                            * that the RIP will allocate before renderering
                            * page. In the multi-process version this is
                            * useful to prevent all avail mem going to bands
                            * and none for pipelining. 0 means ignore.
                            */
  USERVALUE MaxBandMemory; /* maximum total size of the raster bands, in MB */
  int32 BaseMapSize;       /* Minimum size of the basemap */
  int32 DisplayListUsed; /* Amount of display list used, in
                          * bytes. Actually goes up in chunks. */

  USERVALUE ScreenAngles[ 4 ] ; /* C M Y K angles (all 0.0 implies don't know) */
  int32 AsyncMemorySize ; /* Size of memory reserved for asynchronous
                           * postcript actions.
                           */
  int32 MaxScreenTable ;  /* maximum size of screen tables in bytes */

  int8  ScreenZeroFromRequest ;
                          /* if true takes yellow screen adjustment from
                             requested frequency rather than
                             calculated frequency */
  uint8 RelevantRepro ; /* whether to execute all setcolorscreen callbacks
                           on the call or not */
  uint8  *CurInputDevice;
  int32  CurInputDeviceLen;
  uint8  *CurOutputDevice;
  int32  CurOutputDeviceLen;
  uint8 *OverrideSpotFunctionName ;
  uint16 OverrideSpotFunctionNameLen ;
  uint8* FontResourceDir;
  int32 FontResourceDirLen;
  uint8 *GenericResourceDir;
  int32 GenericResourceDirLen;
  uint8 *GenericResourcePathSep;
  int32 GenericResourcePathSepLen;
  int32 WaitTimeout;

  USERVALUE OverrideAngle ;
  int32 ScreenLevels ; /* limit the number of gray levels such that if the
                          screen naturally exceeded this number, it would be
                          reduced as close as reasonable to this number */
  int8 AdobeSetHalftone;     /* If FALSE then unused screens are purged from
                              * cache: if they need to be regenerated by
                              * grestore for example, and the halftone dict is
                              * corrupted, an unwanted error will occur.  Set
                              * this TRUE to prevent it, but waste memory
                              * hanging on to unused screens.
                              */
  int32 HalftoneRetention;
  int32 MaxImagePixel ;
  uint8 DetectSeparation;
  int8 HDS;                 /* turns on HDS */
/* Resolutions HDS is allowed for */
#define PARAMS_HDS_DISABLED  (0)
#define PARAMS_HDS_LOWRES    (1)
#define PARAMS_HDS_HIGHRES   (2)
  int8 HMS;                 /* ditto HMS */
  int8 HCS;                 /* ditto HCS */
  int8 HPS;                 /* ditto HPS; */
  int8 HXM;                 /* ditto HXM */
/* Resolutions HXM is allowed for */
#define PARAMS_HXM_DISABLED  (0)
#define PARAMS_HXM_LOWRES    (1)
#define PARAMS_HXM_HIGHRES   (2)
  int8 PoorClippath[PCP_ELEMENTS] ;
                /* Enable certain optimisation with clippath.Index:
                 * 0) use device boundaries as a (virtual) clip
                 * 1) skip paths if rectangles and contained inside each other
                 * 2) deal with paths in strict order
                 * 3) decompose clip =1 sub-path
                 * 4) decompose clip >1 sub-path
                 * 5) decompose eoclip =1 sub-path
                 * 6) decompose eoclip >1 sub-path
                 * 7) coalesce clip path elements to reduce interior paths
                 * 8) immediate clippath on clip/eoclip/iclip/rectclip.
                 */
  uint8 *SourceDate ;
  int32 SourceDateLen ;

  USERVALUE MinScreenDetected;  /* Only screens above this lpi value are detected */
  int8  RevisionPassword;       /* Allows major revision of RIP with earlier dongle */
  int8  DLMS;                   /* Turns on DLMS, aka IDLOM */

  uint8 ForceStrokeAdjust;      /* Whether and how to override the gstate stroke adjust value */
  uint8 ForceRectWidth;         /* If TRUE overrides rectangle width to stop thin rectangles disappearing */
  int8  PDFOut;                 /* Is PDF Output (aka Distiller/Splitter) enabled? */
  int32 MinLineWidth[ 2 ] ;     /* Minimum allowable line width in x,y. */

  int8  TrapProLite;            /* Turns on TrapProLite extra feature */
  int8  TrapPro;                /* Turns on TrapPro extra feature */
  int8  TiffIT;                 /* is TIFF-IT enabled? */
  int32 AccurateRenderThreshold;/* Pixel value to switch character renderer */
  int32 AccurateTwoPassThreshold; /* Pixel value to switch two-pass renderer */
  USERVALUE Type1StemSnap;      /* Offset to add to Type1 stem snapping */
  int8  PostScript;             /* PostScript enabled */
  int8  PDF;                    /* PDF enabled */
  int8  XPS;                    /* XPS enabled */
  int8  ApplyWatermark;         /* Output is watermarked */

  int32 DefaultImageFileCache;  /* Default size of the image file cache buffer in bytes */
  int8  Tiff6;                  /* is TIFF 6.0 enabled? */
  int8  ICC;                    /* Turns on ICC (HIPP) */
  int8  HCMS;                   /* Turns on HCMS */
  int8  HCMSLite;               /* Turns on HCMSLite */
  int8  HCEP;                   /* Turns on HCEP */

  int32 DoDetectScreenAngles ;  /* bool indictating if we detect screen angles. */
  USERVALUE DetectScreenAngles[ 4 ] ;   /* C M Y K angles used for color detection. */
  uint8 AdobeSetFlat ;          /* If setflat obeys Adobes formula. */
  uint8 PoorFlattenpath ;       /* If flattenpath remembers curves or not. */
  uint8 EnableStroker[3];       /* Enable new unified stroking code.
                                 * Too many regressions to turn it all on at
                                 * once, so have 3 independent switches
                                 * 0) Turn on new stroker algorithm
                                 * 1) Enable XPS scan-convert fix
                                 * 2) Enable implict-group fix
                                 */
  uint8 AdobeSetLineJoin ;      /* If replace line joins with curves. */
  int8  HPSTwo;                 /* True if we're using new version of HPS */

  int32 TickleWarnTime;         /* Warn if not tickled for this duration (ms) */

  int32 Picture;                 /* Set as a side effect of image to say whether the
                                   most recent image is a picture */

  uint8 DeviceImageOptimization;/* True if we attempt to use 1:1 image optimization. */

  OBJECT Watermark;             /* A procedure called for each page in
                                 * watermarked RIPs only. Initially NULL and
                                 * can only be set once. Not present in the
                                 * dictionary returned by currentsystemparams.
                                 * All very non-standard, but it's supposed to
                                 * be obfuscated or even semi-secure.
                                 */

  uint8 TransparencyStrategy;   /* 1 = single-pass, 2 = two-pass compositing */

  int8 PlatformPassword;        /* Allows rip to run on better OS with dongle for lesser OS */
  int32 EncryptScreens;         /* Adds disk-saved encrypted screen caches */
  uint8* OSLocale;              /* Host OS and RIP locales */
  int32  OSLocaleLen;
  uint8* RIPLocale;
  int32  RIPLocaleLen;
  uint8* OperatingSystem;
  int32  OperatingSystemLen;

  uint8 StripeImages;          /* If TRUE, images can be striped in low memory situations. */
  uint8 OptimizeClippedImages; /* if TRUE, clipped out image data will not be stored or color converted. */
  int32 Rainstorm;             /* Control Rainstorm functionality */
  int32 DLBanding;             /* Control DL Banding functionality */

  int32 Pipelining;            /* Turns on pipelining */

  int32 HVDExternal;           /* Turns on HVD (external) */
  int32 HVDInternal;           /* Turns on HVD (internal) */

  Bool PoorShading;            /* Rather than using Gourauds, render radial
                                * shfills using fills and axial shfills using images;
                                * this can produce smaller PGB's for RLE output.
                                */
} SYSTEMPARAMS ;


typedef struct mischookparams {
  OBJECT ImageRGB ;             /* RGB image found */
  OBJECT CompressJPEG ;         /* JPEG compressed image found */
  OBJECT ImageLowRes ;          /* too low res image found */
  OBJECT FontBitmap ;           /* bitmap font found */
  OBJECT SecurityChecked ;      /* serialnumber has been called */

  USERVALUE ImageLowLW ;        /* thresholds for LW and CT data in dpi */
  USERVALUE ImageLowCT ;        /* for ImageLowRes misc hook tests */
} MISCHOOKPARAMS ;

#define MISCHOOKPARAMENTRIES 7

#define TYPE1STEMSNAPDISABLED (FLT_MAX)

/* Functions exported by params.c */

struct core_init_fns ;

void systemparams_C_globals(struct core_init_fns *fns);

void userparams_C_globals(struct core_init_fns *fns);

void mischookparams_C_globals(struct core_init_fns *fns);

/** \brief Push a dictionary with the current value of a set of parameters onto
    the operand stack. */
Bool currentparams(ps_context_t *context, module_params_t *plist) ;

/** \brief Extract parameters from a dictionary, and set the corresponding
    internal parameter structures.

    The type of the context for \c setparams differs from \c currentparams
    because it is called directly on a dictionary, and does not rely on the
    operandstack or any other PostScript context. */
Bool setparams(corecontext_t *context, OBJECT *thed, module_params_t *plist) ;

Bool check_sys_password(corecontext_t *context, OBJECT *passwdo) ;
Bool check_job_password(corecontext_t *context, OBJECT *passwdo) ;
Bool set_job_password(corecontext_t *context, OBJECT *passwdo) ;
Bool setjobnameandflag(corecontext_t *context, OBJECT *thes, OBJECT *thef);

/** Convert a name object the a scan conversion rule. */
Bool scanconvert_from_name(OBJECT *theo, uint32 disallow, uint8 *rule) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
