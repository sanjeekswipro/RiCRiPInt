/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:params.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Handling systemparams, userparams, pdfparams, hookparams et al.
 */

#include "core.h"
#include "params.h"
#include "coreinit.h"
#include "pscontext.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"           /* HqMemCmp */
#include "coreparams.h"         /* module_params_t */
#include "custlib.h"            /* ProductNameParam */
#include "asyncps.h"           /* init_async_memory */
#include "dicthash.h"           /* systemdict */
#include "dictops.h"            /* enddictmark_ */
#include "dongle.h"             /* ripLicenseIsFromLDK */
#include "execops.h"            /* setup_pending_exec */
#include "fontcache.h"          /* fontcache_clear */
#include "gcscan.h"             /* ps_scan_field */
#include "genkey.h"             /* GNKEY_FEATURE_* */
#include "graphics.h"           /* GSTATE */
#include "gs_cache.h"           /* GSC_ENABLE_ALL_COLOR_CACHES */
#include "gs_spotfn.h"          /* findSpotFunctionObject */
#include "gsccalib.h"           /* gsc_validateCalibrationArray */
#include "gscdevci.h"           /* gsc_setoverprintmode */
#include "gschtone.h"           /* gsc_redo_setscreen */
#include "gstate.h"             /* gs_InvalidColorForSaveLevel */
#include "gu_cons.h"            /* checkbbox */
#include "intscrty.h"           /* RIP_DenyFeature */
#include "mmcompat.h"           /* mm_alloc_static */
#include "monitor.h"            /* monitorf */
#include "namedef_.h"           /* NAME_* */
#include "ndisplay.h"           /* SC_RULE_* */
#include "pathops.h"            /* ForceStrokeAdjust values */
#include "psvm.h"               /* DEFAULTVMTHRESHOLD */
#include "scanconv.h"           /* AR_MAXSIZE */
#include "security.h"           /* SWpermission */
#include "spdetect.h"           /* detect_setsystemparam_separation */
#include "stackops.h"           /* cleartomark_ */
#include "statops.h"            /* setjobtimeout */
#include "swcopyf.h"            /* swcopyf */
#include "swenv.h"              /* get_oslocale */
#include "swerrors.h"           /* error_handler */
#include "swstart.h"            /* SWSTART */
#include "timebomb.h"           /* IsWaterMarkRIP */
#include "vndetect.h"           /* VDL_Simple */
#include "basemap.h"            /* BASEMAP_DEFAULT_SIZE */
#include "display.h"
#include "corejob.h"
#include "control.h" /* dosomeaction */

static uint8 SysPasswdString[ MAX_PASSWORD_LENGTH ] ;
static uint8 JobPasswdString[ MAX_PASSWORD_LENGTH ] ;
static uint8 CurInputDeviceString[ MAX_DEVICE_NAME_LENGTH ] ;
static uint8 CurOutputDeviceString[ MAX_DEVICE_NAME_LENGTH ] ;
static uint8 OverrideSFString[ MAX_SF_NAME_LENGTH ] = { 0 } ;
static uint8 FontResourceDirString[ MAX_RESOURCE_DIR_LENGTH ];
static uint8 GenericResourceDirString[ MAX_RESOURCE_DIR_LENGTH ];
static uint8 GenericResourcePathSepString[ MAX_RESOURCE_DIR_LENGTH ];

static SYSTEMPARAMS system_params ;

/* defined in makedate.c */
extern int32 build_time ;
extern int8  *build_date ;

#define ARENDERTHRESHOLD (140) /* AccurateRenderThreshold default pixel size */
#define ATWOPASSTHRESHOLD (20) /* AccurateTwoPassThreshold default pixel size */

/** Set the quad-state using the passed name number, which must be one of
NAME_DefaultTrue, NAME_DefaultFalse, NAME_ForceTrue, NAME_ForceFalse.
*/
Bool quadStateSetFromName(QuadState* self, int16 nameNumber)
{
  switch (nameNumber) {
  default:
    /* Invalid name. */
    return FALSE;

  case NAME_DefaultTrue:
  case NAME_DefaultFalse:
  case NAME_ForceTrue:
  case NAME_ForceFalse:
    self->nameNumber = nameNumber;
    break;
  }
  return TRUE;
}

/** Apply the quad-state to the passed user-specified value, returning the value
that should be used.
If 'userValue' is NULL, it is assumed that the user specified no value (allowing
a default state to be used). If the user specified a value, only a force state
will override it.
*/
Bool quadStateApply(QuadState* self, Bool* userValue)
{
  HQASSERT(self != NULL, "quadStateApply - 'self' cannot be NULL");

  switch (self->nameNumber) {
    default:
      HQFAIL("quadStateApply - Invalid value.");
      return FALSE;

    /* Check for overrides. */
    case NAME_ForceTrue:
      return TRUE;
    case NAME_ForceFalse:
      return FALSE;

    /* Check for unspecified user value. */
    case NAME_DefaultFalse:
      if (userValue == NULL)
        return FALSE;
      break;
    case NAME_DefaultTrue:
      if (userValue == NULL)
        return TRUE;
      break;
  }

  /* No override, and the user specified a value, so return it. */
  return *userValue;
}

/** Convenience method to apply a QuadState to a PostScript object (which must
be NULL or an OBOOLEAN).
*/
Bool quadStateApplyToObject(QuadState* self, OBJECT* userValue)
{
  HQASSERT(self != NULL, "quadStateApply - 'self' cannot be NULL");
  HQASSERT(userValue == NULL || oType(*userValue) == OBOOLEAN,
           "quadStateApply - 'userValue' must be NULL or a BOOLEAN");

  if (userValue == NULL)
    return quadStateApply(self, NULL);
  else {
    Bool flag = oBool(*userValue);
    return quadStateApply(self, &flag);
  }
}

/** Convenience function to convert an object to a scan conversion rule. */
Bool scanconvert_from_name(OBJECT *theo, uint32 disallow, uint8 *rule)
{
  uint8 tmp ;

  HQASSERT(theo, "No object for scan conversion rule") ;
  HQASSERT(rule, "Nowhere to put scan conversion rule") ;

  if ( oType(*theo) != ONAME )
    return FALSE ;

  switch ( oNameNumber(*theo) ) {
  case NAME_RenderHarlequin:
    tmp = SC_RULE_HARLEQUIN ;
    break ;
  case NAME_RenderTouching:
    tmp = SC_RULE_TOUCHING ;
    break ;
  case NAME_RenderTesselating:
    tmp = SC_RULE_TESSELATE ;
    break ;
  case NAME_RenderBresenham:
    tmp = SC_RULE_BRESENHAM ;
    break ;
  case NAME_RenderCenter:
    tmp = SC_RULE_CENTER ;
    break ;
  default:
    return FALSE ;
  }

  if ( (1 << tmp) & disallow ) /* Are we allowed this one? */
    return FALSE ;

  *rule = tmp ;
  return TRUE ;
}

static Bool currentparam(ps_context_t *pscontext, module_params_t *plist)
{
  corecontext_t *context = pscontext->corecontext ;
  OBJECT * keyo, result = OBJECT_NOTVM_NOTHING ;
  NAMECACHE * pName;
  uint16 name ;

  if (isEmpty(operandstack))
    return error_handler(STACKUNDERFLOW);

  keyo = theTop(operandstack);

  if ( oType(*keyo) == ONAME ) {
    pName = oName(*keyo);
  } else if ( oType(*keyo) == OSTRING ) {
    pName = cachename(oString(*keyo), theLen(*keyo));
    if (pName == NULL)
      return FALSE;
  } else {
    return error_handler(UNDEFINED);
  }

  if ( theINameNumber(pName) < 0 )
    return error_handler(UNDEFINED);

  name = (uint16)theINameNumber(pName) ;

  /* Run over each module's parameters in turn, until we find the name we're
     after. If the getter leaves the return value at ONOTHING, its value is
     not returned. */
  theTags(result) = ONOTHING ;

  while ( plist ) {
    if ( plist->get_param ) {
      if ( !(plist->get_param)(context, name, &result) )
        return FALSE ;

      if ( oType(result) != ONOTHING ) {
        Copy(keyo, &result) ;
        return TRUE ;
      }
    }

    plist = plist->next ;
  }

  return error_handler(UNDEFINED) ;
}


Bool currentparams(ps_context_t *pscontext, module_params_t *plist)
{
  corecontext_t *context = pscontext->corecontext ;
  Bool res = TRUE;

  if (! mark_(pscontext))
    return FALSE;

  /* Run over each module's parameters in turn, getting values for each. If the
     getter leaves the return value at ONOTHING, its value is not returned. */
  while ( res && plist ) {
    if ( plist->get_param ) {
      NAMETYPEMATCH *match = plist->match ;

      HQASSERT(match, "parameter accessor has no params") ;

      while ( theISomeLeft(match) ) {
        uint16 name = CAST_TO_UINT16(match->name & ~OOPTIONAL) ;
        OBJECT result = OBJECT_NOTVM_NOTHING ;

        res = (plist->get_param)(context, name, &result);
        if ( !res ) {
          break;
        }

        if ( oType(result) != ONOTHING ) {
          oName(nnewobj) = &system_names[name] ;
          res = push2(&nnewobj, &result, &operandstack);
          if ( !res ) {
            break;
          }
        }

        ++match ;
      }
    }

    plist = plist->next ;
  }

  res = res && enddictmark_(pscontext);
  if ( !res ) {
    /* Tidy up the operand stack */
    (void)cleartomark_(pscontext);
  }
  return res ;
}


Bool setparams(corecontext_t *context, OBJECT *thed, module_params_t *plist)
{
  /* Match dictionary against each module's parameters in turn, and call
     setter function for matches. */
  while ( plist ) {
    if ( plist->set_param ) {
      NAMETYPEMATCH *match = plist->match ;

      HQASSERT(match, "Param accessor has no params") ;
      if (! dictmatch(thed, match) )
        return FALSE ;

      while ( theISomeLeft(match) ) {
        if ( match->result != NULL ) {
          uint16 name = CAST_TO_UINT16(match->name & ~OOPTIONAL) ;
          if ( !(plist->set_param)(context, name, match->result) )
            return FALSE ;
        }
        ++match ;
      }

      /* A final call is made to the setter function with an invalid name, to
         allow finalisation of changed parameters. The PostScript
         systemparams setter uses this to rebuild screens. */
      if ( !(plist->set_param)(context, NAMES_COUNTED, NULL) )
        return FALSE ;
    }

    plist = plist->next ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            check_sys_password    author:              Luke Tunmer
   creation date:       09-Oct-1991           last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool check_sys_password(corecontext_t *context, OBJECT *passwdo)
{
  SYSTEMPARAMS *systemparams = context->systemparams ;

  if ( systemparams->SystemPasswordLen == 0 )
    return TRUE ;

  if ( oType( *passwdo ) == OSTRING ) {
    if ( HqMemCmp( oString( *passwdo ) , theLen(*passwdo) ,
                   systemparams->SystemPassword , systemparams->SystemPasswordLen ) == 0 )
      return TRUE ;
  } else if ( oType( *passwdo ) == OINTEGER ) {
    uint8 temp[ MAX_PASSWORD_LENGTH ] ;
    int32 len ;

    swcopyf(temp , ( uint8 * )"%d" , oInteger( *passwdo )) ;
    len = strlen_int32(( char * )temp ) ;
    if ( HqMemCmp( temp , len , systemparams->SystemPassword ,
                   systemparams->SystemPasswordLen ) == 0 )
      return TRUE ;
  }

  return FALSE ;
}


/* ----------------------------------------------------------------------------
   function:            check_job_password    author:              Luke Tunmer
   creation date:       09-Oct-1991           last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool check_job_password(corecontext_t *context, OBJECT *passwdo)
{
  SYSTEMPARAMS *systemparams = context->systemparams ;

  if ( systemparams->StartJobPasswordLen == 0 )
    return TRUE ;

  if ( oType( *passwdo ) == OSTRING ) {
    if ( HqMemCmp( oString( *passwdo ) , theLen(*passwdo) ,
                   systemparams->StartJobPassword ,
                   systemparams->StartJobPasswordLen ) == 0 )
      return TRUE ;
  } else if ( oType( *passwdo ) == OINTEGER ) {
    uint8 temp[ MAX_PASSWORD_LENGTH ] ;
    int32 len ;

    swcopyf(temp , ( uint8 * )"%d" , oInteger( *passwdo )) ;
    len = strlen_int32(( char * )temp ) ;
    if ( HqMemCmp( temp , len , systemparams->StartJobPassword ,
                   systemparams->StartJobPasswordLen ) == 0 )
      return TRUE ;
  }

  return FALSE ;
}

/** GC root for the hook params. */
static mps_root_t MiscHookParamsRoot;

/** GC scanning function for the hook params. */
mps_res_t MPS_CALL scanMiscHookParams(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  MISCHOOKPARAMS *params;

  UNUSED_PARAM( size_t, s );
  params = (MISCHOOKPARAMS *)p;
  res = ps_scan_field( ss, &params->ImageRGB );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->CompressJPEG );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->ImageLowRes );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->FontBitmap );
  if ( res != MPS_RES_OK ) return res;
  res = ps_scan_field( ss, &params->SecurityChecked );
  return res;
}

/** Initialization for the hook parameters. */
static Bool mischookparams_swinit(struct SWSTART *params)
{
  corecontext_t *context = get_core_context() ;
  MISCHOOKPARAMS *mischookparams ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  context->mischookparams = mischookparams = mm_alloc_static(sizeof(MISCHOOKPARAMS)) ;
  if ( mischookparams == NULL )
    return FALSE ;

  /* Values for misc hooks. */
  mischookparams->ImageLowLW = -1.0f;
  mischookparams->ImageLowCT = -1.0f;

  /* Struct copies to set slot properties */
  mischookparams->ImageRGB = onull;
  mischookparams->CompressJPEG = onull;
  mischookparams->ImageLowRes = onull;
  mischookparams->FontBitmap = onull;
  mischookparams->SecurityChecked = onull;

  if ( mps_root_create( &MiscHookParamsRoot, mm_arena, mps_rank_exact(),
                        0, scanMiscHookParams, mischookparams, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/* finishMiscHookParams - deinitialization for the hook parameters */
static void mischookparams_finish(void)
{
  mps_root_destroy( MiscHookParamsRoot );
}

void mischookparams_C_globals(core_init_fns *fns)
{
  MiscHookParamsRoot = NULL ;
  fns->swinit = mischookparams_swinit ;
  fns->finish = mischookparams_finish ;
}

/* ----------------------------------------------------------------------------
   function:            set_sys_password  author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool set_sys_password(SYSTEMPARAMS *systemparams, OBJECT *passwdo)
{
  int32 len ;

  switch ( oType( *passwdo )) {
  case OINTEGER :
    {
      uint8 temp[ MAX_PASSWORD_LENGTH ] ;

      swcopyf(temp , ( uint8 * )"%d" , oInteger( *passwdo )) ;
      len = strlen_int32(( char * )temp ) ;
      if ( len > MAX_PASSWORD_LENGTH )
        return error_handler( LIMITCHECK ) ;
      systemparams->SystemPasswordLen = len ;
      HqMemMove( systemparams->SystemPassword , temp , len ) ;
      break ;
    }
  case OSTRING :
    len = theLen(*passwdo) ;
    if ( len + 1 > MAX_PASSWORD_LENGTH )
      return error_handler( LIMITCHECK ) ;
    systemparams->SystemPasswordLen = len ;
    HqMemMove( systemparams->SystemPassword , oString( *passwdo ) , len);
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            set_override_sf_name  author:              Julian Boyfield
   creation date:       13-July-1993          last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool set_override_sf_name(SYSTEMPARAMS *systemparams, OBJECT *obj)
{
  int32 len ;
  uint8 *addr ;
  NAMECACHE *sfname ;

  switch ( oType( *obj )) {
  case OSTRING :
    len = theLen(*obj) ;
    addr = oString( *obj ) ;
    break ;
  case ONAME:
    len = theINLen( oName( *obj ) ) ;
    addr = theICList( oName( *obj ) ) ;
    break ;
  case ONULL:
    systemparams->OverrideSpotFunctionNameLen = 0 ;
    return TRUE ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( len + 1 > MAX_SF_NAME_LENGTH )
    return error_handler( LIMITCHECK ) ;

  if ( len != 0 ) {
    if ( ( sfname = cachename( addr, len ) ) == NULL )
      return error_handler( VMERROR ) ;

    if ( findSpotFunctionObject(sfname) == NULL )
      return error_handler( RANGECHECK ) ;

    HqMemMove( systemparams->OverrideSpotFunctionName , addr , len) ;
  }
  systemparams->OverrideSpotFunctionNameLen = (uint16)len ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            set_job_password  author:              Luke Tunmer
   creation date:       21-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool set_job_password(corecontext_t *context, OBJECT *passwdo)
{
  SYSTEMPARAMS *systemparams = context->systemparams ;
  int32 len ;

  switch ( oType( *passwdo )) {
  case OINTEGER :
    {
      uint8 temp[ MAX_PASSWORD_LENGTH ] ;

      swcopyf(temp , ( uint8 * )"%d" , oInteger( *passwdo )) ;
      len = strlen_int32(( char * )temp ) ;
      if ( len > MAX_PASSWORD_LENGTH )
        return error_handler( LIMITCHECK ) ;
      systemparams->StartJobPasswordLen = len ;
      HqMemMove( systemparams->StartJobPassword , temp , len ) ;
      break ;
    }
  case OSTRING :
    len = theLen(*passwdo) ;
    if ( len + 1 > MAX_PASSWORD_LENGTH )
      return error_handler( LIMITCHECK ) ;
    systemparams->StartJobPasswordLen = len ;
    HqMemMove( systemparams->StartJobPassword , oString( *passwdo ) , len);
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }
  return TRUE ;
}

/** Refer to the JobName user parameter in the extensions manual.
*/
Bool setjobnameandflag(corecontext_t *context, OBJECT *thes, OBJECT *thef)
{
  OBJECT * statusdict, *ForDSC, * TitleDSC;
  /* DSC = Document Structuring Conventions */

  static uint8 docstring[] = "; document: ";

  HQASSERT( thes , "thes is null" ) ;
  HQASSERT( oType( *thes ) == OSTRING ,
            "expecting thes to be an OSTRING" ) ;
  HQASSERT( thef == NULL || oType( *thef ) == OBOOLEAN ,
            "expecting thef to be an OBOOL" ) ;

  /* Now set the appropriate value in statusdict:
     level 2 mac print drivers appear to put the senders name in
     the JobName. The only place where the title appears
     is in the Title comment. There is some comment parsing
     to keep this and the For comment, so if the %%For is the same
     as JobName and %%Title isnt empty we can reconstruct an appropriate
     statusdict string and by magic it all works as it used to.
     JobName may also be set to (xxx; page n of m) as well where we
     have "%%For: xxx", so the check is a bit more complex -
     either it matches exactly, or it matches at least the %%For as a
     prefix, followed by ';'.
     */
  statusdict = fast_extract_hash_name(& systemdict, NAME_statusdict);
  if (! statusdict)
    return error_handler (UNREGISTERED);

  oName(nnewobj) = system_names + NAME_ForDSC;
  ForDSC = extract_hash(statusdict, & nnewobj);
  oName(nnewobj) = system_names + NAME_TitleDSC;
  TitleDSC = extract_hash(statusdict, & nnewobj);

  if (ForDSC && (oType (*ForDSC) != OSTRING || theLen(*ForDSC) == 0))
    ForDSC = NULL;
  if (TitleDSC && (oType (*TitleDSC) != OSTRING || theLen(*TitleDSC) == 0))
    TitleDSC = NULL;

  oName(nnewobj) = system_names + NAME_jobname;

  if (theLen(*thes) == 0 && TitleDSC) {
    /* If the passed name is empty, and we have a valid value for %%Title,
    build as much of the name as possible. */
    if (ForDSC) {
      /* Create a full "[ForDSC]; document: [TitleDSC]" job name. */
      if ( !ps_string(thes, NULL, theLen(*ForDSC) + theLen(*TitleDSC) +
                                  strlen_int32((char*)docstring)) )
        return FALSE;

      /* Note the trailing '%N' in the format string to prevent a null
      terminator being added to the target string. */
      swcopyf(oString(*thes), (uint8*)"%.*s%s%.*s%N", theLen(*ForDSC),
              oString(*ForDSC), docstring, theLen(*TitleDSC),
              oString(*TitleDSC));
    }
    else {
      /* Just use the TitleDSC string for the job name. */
      if ( !ps_string(thes, oString(*TitleDSC), theLen(*TitleDSC)) )
        return FALSE;
    }
  }
  else {
    /* Otherwise, if both the ForDSC and TitleDSC are valid, and the passed
    job name matches ForDSC or starts with "[ForDSC];", rebuild the job name to
    "[ForDSC]; document: [TitleDSC]". */
    if (ForDSC && TitleDSC
        && (theLen(*ForDSC) == theLen(*thes) ||
            (theLen(*ForDSC) < theLen(*thes) &&
             (char) oString(*thes)[theLen(*ForDSC)] == ';'))
        && HqMemCmp(oString(*thes), theLen(*ForDSC),
                    oString(*ForDSC), theLen(*ForDSC)) == 0) {

      if ( !ps_string(thes, NULL, theLen(*ForDSC) + theLen(*TitleDSC) +
                                  strlen_int32((char*)docstring)) )
        return FALSE;

      /* Note the trailing '%N' in the format string to prevent a null
      terminator being added to the target string. */
      swcopyf(oString(*thes), (uint8 *) "%.*s%s%.*s%N",
              theLen(*ForDSC), oString(*ForDSC), docstring,
              theLen(*TitleDSC), oString(*TitleDSC));
    }
  }

  if (!insert_hash(statusdict, &nnewobj, thes))
    return error_handler(UNREGISTERED);

  corejob_name(context->page->job, oString(*thes), theLen(*thes));

  oName(nnewobj) = system_names + NAME_jobnameisfilename;
  if (thef) {
    if (!insert_hash(statusdict, &nnewobj, thef))
      return error_handler(UNREGISTERED);
  }
  else {
    (void) remove_hash(statusdict, &nnewobj, FALSE);
  }

  /* the job name has changed: as this may be after any setpagedevice calls
     (which set the printer state - ie. gives this info to the
     outside world), set the printer state now */

  oName(nnewobj) = system_names + NAME_sendprinterstate;
  thes = extract_hash(statusdict, & nnewobj);
  if (thes) { /* it ought to be there */
    if (! setup_pending_exec(thes, TRUE /* do it now */))
      return FALSE;
  }

  return TRUE ;
}

/** Convert the passed 'value' (which may be a name or a boolean) into a
QuadState value.
Any name directly matching one of the quad state values is accepted directly.
The boolean 'true' is mapped to ForceTrue. The boolean 'false' is mapped to
DefaultFalse.
Returns false if value cannot be mapped to a valid QuadState.
*/
static Bool interpretInterpolateAll(OBJECT value, QuadState* result)
{
  HQASSERT(result != NULL, "interpretInterpolateAll - result cannot be false.");

  switch (oType(value)) {
    /* Unacceptable type. */
    default:
      return error_handler(TYPECHECK);

    /* A boolean - map it to a suitable QuadState. */
    case OBOOLEAN:
      if (oBool(value))
        result->nameNumber = NAME_ForceTrue;
      else
        result->nameNumber = NAME_DefaultFalse;
      break;

    /* A name - convert it to a quad state. */
    case ONAME:
      if (! quadStateSetFromName(result, oName(value)->namenumber))
        return error_handler(RANGECHECK);
      break;
  }

  return TRUE;
}

/* ---------------------------------------------------------------------------*/

static NAMETYPEMATCH user_match[] = {
  { NAME_MaxFontItem     | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MinFontCompress | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxUPathItem    | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxFormItem     | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxPatternItem  | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxScreenItem   | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxOpStack      | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxDictStack    | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxExecStack    | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxLocalVM      | OOPTIONAL, 1, { OINTEGER }},
  { NAME_VMReclaim       | OOPTIONAL, 1, { OINTEGER }},
  { NAME_VMThreshold     | OOPTIONAL, 1, { OINTEGER }},

  { NAME_ImpositionForceErasePage | OOPTIONAL, 1, { OBOOLEAN }},

  { NAME_JobName     | OOPTIONAL, 1, { OSTRING }},
  { NAME_JobTimeout  | OOPTIONAL, 1, { OINTEGER }},
  { NAME_WaitTimeout | OOPTIONAL, 1, { OINTEGER }},

  { NAME_ForceFontCompress      | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
  { NAME_NeverRender            | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_VignetteDetect         | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_VignetteMinFills       | OOPTIONAL, 1, { OINTEGER }},
  { NAME_VignetteLinearMaxStep  | OOPTIONAL, 1, { OREAL }},
  { NAME_VignetteLogMaxStep     | OOPTIONAL, 1, { OREAL }},
  { NAME_AdobeFilePosition      | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ShadingAntiAliasDefault | OOPTIONAL, 1, { ONAME }},
  { NAME_ResolutionFactor       | OOPTIONAL, 3, { OINTEGER, OREAL, ONULL }},
  { NAME_IdiomRecognition       | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_MinShadingSize         | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_GouraudLinearity       | OOPTIONAL, 1, { OINTEGER }},
  { NAME_BlendLinearity         | OOPTIONAL, 1, { OINTEGER }},
  { NAME_HalftoneMode           | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxSuperScreen         | OOPTIONAL, 1, { OINTEGER }},
  { NAME_RejectPreseparatedJobs | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AutomaticBinding       | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AccurateScreens        | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AdobeArct              | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_RecombineWeakMergeTest | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_RecombineTrapWidth     | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_RecombineObject        | OOPTIONAL, 1, { OINTEGER }},
  { NAME_RecombineObjectThreshold | OOPTIONAL, 1, { OINTEGER }},
  { NAME_RecombineObjectProportion | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_BackdropReserveSize    | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_BackdropResourceLimit  | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_UseScreenCacheName     | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_SilentFontFault        | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_InterpolateAllImages   | OOPTIONAL, 2, { ONAME, OBOOLEAN }},
  { NAME_InterpolateAllMasks    | OOPTIONAL, 2, { ONAME, OBOOLEAN }},
  { NAME_MinSmoothness          | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_MaxSmoothness          | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_ShadingAntiAliasSize   | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_ShadingAntiAliasFactor | OOPTIONAL, 2, { OINTEGER, OREAL }},
  { NAME_SnapImageCorners       | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_PatternOverprintOverride | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_MaxGouraudDecompose    | OOPTIONAL, 1, { OINTEGER }},

  { NAME_EnablePseudoErasePage  | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_StrokeScanConversion   | OOPTIONAL, 1, { ONAME }},
  { NAME_CharScanConversion     | OOPTIONAL, 1, { ONAME }},
  { NAME_PCLScanConversion      | OOPTIONAL, 1, { ONAME }},
  { NAME_RetainedRasterCompressionLevel  | OOPTIONAL, 1, { OINTEGER }},
  { NAME_ImageDecimation | OOPTIONAL, 2, { ONULL, ODICTIONARY }},
  { NAME_ImageDownsampling      | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverridePatternTilingType | OOPTIONAL, 1, {OINTEGER}},
  { NAME_AlternateJPEGImplementations | OOPTIONAL, 1, {OBOOLEAN}},
  DUMMY_END_MATCH
};

/* ----------------------------------------------------------------------------
   function:            setuserparams     author:              Luke Tunmer
   creation date:       18-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool setuserparams_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  OBJECT *thed ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  thed = theTop( operandstack ) ;

  if ( oType(*thed) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! oCanRead(*oDict(*thed)) )
    if ( ! object_access_override(oDict(*thed)))
      return error_handler( INVALIDACCESS ) ;

  if ( !setparams(context, thed, context->userparamlist) )
    return FALSE ;

  pop(&operandstack) ;
  return TRUE ;
}

static Bool ps_set_userparam(corecontext_t *context, uint16 name, OBJECT *theo)
{
  USERPARAMS *userparams = context->userparams ;
  int32 theval ;

  static float minsmoothness, maxsmoothness ;
  static enum { NO_SMOOTHNESS_LIMIT = 0x0,
                MIN_SMOOTHNESS_LIMIT = 0x1,
                MAX_SMOOTHNESS_LIMIT = 0x2 } smoothness_limit = 0 ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {

  case NAME_MaxFontItem:
    userparams->MaxFontItem = oInteger(*theo) ;
    break ;

  case NAME_MinFontCompress:
    userparams->MinFontCompress = oInteger(*theo) ;
    break ;

  case NAME_MaxUPathItem:
    userparams->MaxUPathItem = oInteger(*theo) ;
    break ;

  case NAME_MaxFormItem:
    userparams->MaxFormItem = oInteger(*theo) ;
    break ;

  case NAME_MaxPatternItem:
    userparams->MaxPatternItem = oInteger(*theo) ;
    break ;

  case NAME_MaxScreenItem:
    userparams->MaxScreenItem = oInteger(*theo) ;
    break ;

  case NAME_MaxOpStack:
    theval = oInteger(*theo) ;
    if ( theval <= 0 )
      return error_handler( RANGECHECK ) ;
    userparams->MaxOpStack = operandstack.limit =
      ( theval + FRAMESIZE - 1 ) & ~( FRAMESIZE - 1 ) ;
    break ;

  case NAME_MaxDictStack:
    theval = oInteger(*theo) ;
    if ( theval <= 0 )
      return error_handler( RANGECHECK ) ;
    userparams->MaxDictStack = dictstack.limit =
      ( theval + FRAMESIZE - 1 ) & ~( FRAMESIZE - 1 ) ;
    break ;

  case NAME_MaxExecStack:
    theval = oInteger(*theo) ;
    if ( theval <= 0 )
      return error_handler( RANGECHECK ) ;
    userparams->MaxExecStack = executionstack.limit =
      ( theval + FRAMESIZE - 1 ) & ~( FRAMESIZE - 1 ) ;
    break ;

  case NAME_MaxLocalVM:
    userparams->MaxLocalVM = oInteger(*theo) ;
    break ;

  case NAME_VMReclaim:
    theval = oInteger(*theo) ;
    if ( theval >= -2 && theval <= 0 )
      userparams->VMReclaim = theval;
    break ;

  case NAME_VMThreshold:
    theval = oInteger(*theo) ;
    if ( theval >= -1 ) {
      double limit;

      limit = mm_set_gc_threshold( (double)theval, &dosomeaction );
      userparams->VMThreshold = ( limit > MAXINT32 ) ? MAXINT32 : (int32)limit;
    }
    break ;

  case NAME_ImpositionForceErasePage:
    userparams->ImpositionForceErasePage = (int8)oBool(*theo) ;
    break ;

  case NAME_JobName:
    if ( !setjobnameandflag(context, theo, NULL) )
      return FALSE ;
    break ;

  case NAME_JobTimeout:
    {
      int32 new_timeout = max(oInteger(*theo), 0);
      setjobtimeout(context, new_timeout);
    }
    break ;

  case NAME_WaitTimeout:
    {
      /* Set the appropriate value in statusdict */
      OBJECT * statusdict = fast_extract_hash_name(& systemdict, NAME_statusdict);
      if (! statusdict)
        return error_handler (UNREGISTERED);
      oName(nnewobj) = system_names + NAME_waittimeout;
      oInteger(*theo) = max(oInteger(*theo), 0);
      if (!insert_hash(statusdict, &nnewobj, theo))
        return error_handler (UNREGISTERED);
    }
    break ;

  case NAME_ForceFontCompress:
    /* a bit more complex than most - an array of 10 numbers */
    if ( theLen(*theo) != MAX_FORCEFONTCOMPRESS)
      return error_handler (RANGECHECK);

    {
      int32 i ;
      int32 values[ MAX_FORCEFONTCOMPRESS ] ;

      theo = oArray(*theo);
      for ( i = 0 ; i < MAX_FORCEFONTCOMPRESS ; ++i ) {
        if ( oType(theo[ i ]) != OINTEGER )
          return FALSE ;
        values[ i ] = oInteger(theo[ i ]) ;
        if ( values[ i ] < 0 )
          return error_handler( RANGECHECK ) ;
      }
      for ( i = 0 ; i < MAX_FORCEFONTCOMPRESS ; ++i )
        userparams->ForceFontCompress[ i ] = values[ i ] ;
    }
    break ;

  case NAME_NeverRender:
     userparams->NeverRender = (int8)oBool(*theo) ;
     break ;

  case NAME_VignetteDetect:
    if ( oBool(*theo) )
      userparams->VignetteDetect = VDL_Simple ;
    else
      userparams->VignetteDetect = VDL_None ;
    break ;

  case NAME_VignetteMinFills:
    userparams->VignetteMinFills = (int8)oInteger(*theo) ;
    break ;

  case NAME_VignetteLinearMaxStep:
    userparams->VignetteLinearMaxStep = oReal(*theo) ;
    break ;

  case NAME_VignetteLogMaxStep:
    userparams->VignetteLogMaxStep = oReal(*theo) ;
    break ;

  case NAME_AdobeFilePosition:
    userparams->AdobeFilePosition = (int8)oBool(*theo) ;
    break ;

  case NAME_ShadingAntiAliasDefault:
    if (! quadStateSetFromName(&userparams->ShadingAntiAliasDefault,
                               oName(*theo)->namenumber))
      EMPTY_STATEMENT() ; /* Silently ignore any unrecognised value. */
    break;

  case NAME_ResolutionFactor:
        /* null sets to -1, otherwise must be positive */
    if ( oType(*theo) == OREAL ) {
      userparams->ResolutionFactor = oReal(*theo) ;
      if (userparams->ResolutionFactor <= 0)
        return error_handler( RANGECHECK ) ;
    } else if ( oType(*theo) == OINTEGER ) {
      userparams->ResolutionFactor = (USERVALUE)oInteger(*theo) ;
      if (userparams->ResolutionFactor < 1)
        return error_handler( RANGECHECK ) ;
    } else
      userparams->ResolutionFactor = -1.0f ;
    break ;

  case NAME_IdiomRecognition:
    userparams->IdiomRecognition = (int8)oBool(*theo) ;
    break ;

  case NAME_MinShadingSize: /* control size of smooth shading decomposition */
    if ( oType(*theo) == OINTEGER )
      userparams->MinShadingSize = (USERVALUE)oInteger(*theo) ;
    else
      userparams->MinShadingSize = oReal(*theo) ;
    break ;

  case NAME_GouraudLinearity:
    /* GouraudLinearity - control extra decomposition depth for smooth
       shading linearity testing. */
    theval = oInteger(*theo) ;

    if ( theval < 0 )
      return error_handler(RANGECHECK) ;

    userparams->GouraudLinearity = theval ;
    break ;

  case NAME_BlendLinearity:
    /* BlendLinearity - control extra decomposition depth for smooth shading
       linearity testing. */
    theval = oInteger(*theo) ;

    if ( theval < 0 )
      return error_handler(RANGECHECK) ;

    userparams->BlendLinearity = theval ;
    break ;

  case NAME_HalftoneMode:
    theval = oInteger(*theo) ;
    if ( theval  < 0 || theval > 2 )
      return error_handler( RANGECHECK );
    /* Only support mode 0 for now, so no need to change anything: this is
       legal wrt the spec, page 245 of 3010 */
    /* userparams->HalftoneMode = (uint8)theval ; */
    break ;

  case NAME_MaxSuperScreen:
    theval = oInteger(*theo) ;
    if ( theval < 0 )
      theval = 0;
    userparams->MaxSuperScreen = theval ;
    break ;

  case NAME_RejectPreseparatedJobs:
    userparams->RejectPreseparatedJobs = (int8)oBool(*theo) ;
    break ;

  case NAME_AutomaticBinding:
    userparams->AutomaticBinding = (int8)oBool(*theo) ;
    break ;

  case NAME_AccurateScreens:
    userparams->AccurateScreens = (int8)oBool(*theo) ;
    break ;

  case NAME_AdobeArct:
    userparams->AdobeArct = (int8)oBool(*theo) ;
    break ;

  case NAME_RecombineWeakMergeTest:
    userparams->RecombineWeakMergeTest = (int8)oBool(*theo) ;
    break ;

  case NAME_RecombineTrapWidth:
    {
      USERVALUE trapWidth ;
      trapWidth =
        (USERVALUE)( oType(*theo) == OINTEGER ?
                     oInteger(*theo) :
                     oReal(*theo) ) ;
      /* [ 0 36 ] is the allowable range in Quark */
      if ( trapWidth < 0.0 || trapWidth > 36.0 )
        return error_handler( RANGECHECK ) ;
      userparams->RecombineTrapWidth = trapWidth ;
    }
    break ;

  case NAME_RecombineObject: {
    int32 recombineobject = oInteger(*theo);
    if (recombineobject < 0 || recombineobject > 2)
      return error_handler(RANGECHECK);
    userparams->RecombineObject = recombineobject;
    break;
  }

  case NAME_RecombineObjectThreshold: {
    int32 threshold = oInteger(*theo);
    if (threshold < 0)
      return error_handler(RANGECHECK);
    userparams->RecombineObjectThreshold = threshold;
    break;
  }

  case NAME_RecombineObjectProportion: {
    USERVALUE proportion
      = (USERVALUE)(oType(*theo) == OINTEGER ? oInteger(*theo) : oReal(*theo));
    if (proportion < 0.0f || proportion > 1.0f)
      return error_handler(RANGECHECK);
    userparams->RecombineObjectProportion = proportion;
    break;
  }

  case NAME_BackdropReserveSize:
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK);
    userparams->BackdropReserveSize = oInteger(*theo);
    break ;

  case NAME_BackdropResourceLimit:
    if ( oInteger(*theo) < 0 )
      return error_handler(RANGECHECK);
    userparams->BackdropResourceLimit = oInteger(*theo);
    break ;

  case NAME_UseScreenCacheName:
    userparams->UseScreenCacheName = (int8)oBool(*theo) ;
    break ;

  case NAME_SilentFontFault:
    userparams->SilentFontFault = (int8)oBool(*theo) ;
    break ;

  case NAME_InterpolateAllImages:
    if (! interpretInterpolateAll(*theo, &userparams->InterpolateAllImages))
      return FALSE;
    break ;

  case NAME_InterpolateAllMasks:
    if (! interpretInterpolateAll(*theo, &userparams->InterpolateAllMasks))
      return FALSE;
    break ;

  case NAME_MinSmoothness:
    minsmoothness = (USERVALUE)(oType(*theo) == OINTEGER ?
                                oInteger(*theo) : oReal(*theo)) ;
    smoothness_limit |= MIN_SMOOTHNESS_LIMIT ;
    break ;

  case NAME_MaxSmoothness:
    maxsmoothness = (USERVALUE)(oType(*theo) == OINTEGER ?
                                oInteger(*theo) : oReal(*theo)) ;
    smoothness_limit |= MAX_SMOOTHNESS_LIMIT ;
    break ;

  case NAME_ShadingAntiAliasSize: {
    USERVALUE size = (USERVALUE)(oType(*theo) == OINTEGER ?
                                oInteger(*theo) : oReal(*theo)) ;

    if ( size < 0 )
      return error_handler(RANGECHECK) ;

    userparams->ShadingAntiAliasSize = size ;
    break ;
  }

  case NAME_ShadingAntiAliasFactor: {
    USERVALUE factor = (USERVALUE)(oType(*theo) == OINTEGER ?
                                   oInteger(*theo) : oReal(*theo)) ;

    if ( factor < 0 || factor > 1 )
      return error_handler(RANGECHECK) ;

    userparams->ShadingAntiAliasFactor = factor ;
    break ;
  }

  case NAME_SnapImageCorners:
    userparams->SnapImageCorners = (uint8)oBool(*theo) ;
    break ;

  case NAME_PatternOverprintOverride:
    userparams->PatternOverprintOverride = (int8)oBool(*theo) ;
    break ;

  case NAME_MaxGouraudDecompose:
    /* Set an absolute limit on Gouraud decomposition depth. */
    theval = oInteger(*theo) ;

    if ( theval < 0 )
      return error_handler(RANGECHECK) ;

    userparams->MaxGouraudDecompose = theval ;
    break ;

  case NAME_EnablePseudoErasePage:
    userparams->EnablePseudoErasePage = (int8)oBool(*theo) ;
    break ;

  case NAME_RetainedRasterCompressionLevel: {
    int32 level = oInteger(*theo) ;

    if ( level < 0 || level > 9 )
      return namedinfo_error_handler( RANGECHECK , name , theo ) ;

    userparams->RetainedRasterCompressionLevel = level ;
    break ;
  }

  case NAME_ImageDecimation:
    if (oType(*theo) == ONULL) {
      /* Do the minimum to disable and also allow currentuserparams to work */
      Copy(&userparams->decimation.paramDict, &onull);
      userparams->decimation.enabled = FALSE;
    }
    else {
      enum {
        decimation_Enabled, decimation_MinimumArea,
        decimation_MinimumResolutionPercentage,
        decimation_dummy
      } ;
      NAMETYPEMATCH decimation_match[decimation_dummy + 1] = {
        { NAME_Enabled | OOPTIONAL, 1, { OBOOLEAN }},
        { NAME_MinimumArea | OOPTIONAL, 1, { OINTEGER, OREAL }},
        { NAME_MinimumResolutionPercentage | OOPTIONAL, 1, { OINTEGER, OREAL }},
        DUMMY_END_MATCH
      };
      OBJECT* result;
      DecimationParams* decimation = &userparams->decimation;
      OBJECT enabled = OBJECT_NOTVM_NOTHING,
             minArea = OBJECT_NOTVM_NOTHING,
             minRes = OBJECT_NOTVM_NOTHING;

      if (! dictmatch(theo, decimation_match))
        return FALSE;

      result = decimation_match[decimation_Enabled].result;
      if (result != NULL)
        decimation->enabled = oBool(*result);

      result = decimation_match[decimation_MinimumArea].result;
      if (result != NULL) {
        SYSTEMVALUE minArea = object_numeric_value(result);
        if (minArea < 0)
          return error_handler(RANGECHECK);
        decimation->minimumArea = (uint32)minArea;
      }

      result = decimation_match[decimation_MinimumResolutionPercentage].result;
      if (result != NULL) {
        SYSTEMVALUE percentage = object_numeric_value(result);
        if (percentage < 0)
          return error_handler(RANGECHECK);
        decimation->minimumResolutionPercentage = (uint32)percentage;
      }

      /* Construct a new dictionary that is used solely for passing back to
       * currentuserparams.
       */
      object_store_bool(&enabled, decimation->enabled);
      object_store_integer(&minArea, decimation->minimumArea);
      object_store_integer(&minRes, decimation->minimumResolutionPercentage);

      if (! ps_dictionary(&decimation->paramDict, 3) ||
          ! fast_insert_hash_name(&decimation->paramDict,
                                  NAME_Enabled, &enabled) ||
          ! fast_insert_hash_name(&decimation->paramDict,
                                  NAME_MinimumArea, &minArea) ||
          ! fast_insert_hash_name(&decimation->paramDict,
                                  NAME_MinimumResolutionPercentage, &minRes)) {
        return error_handler(VMERROR);
      }
    }
    break ;

  case NAME_ImageDownsampling:
    userparams->ImageDownsampling = (int8)oBool(*theo) ;
    break ;

  case NAME_StrokeScanConversion:
    if ( !scanconvert_from_name(theo, 0 /*disallow*/,
                                &userparams->StrokeScanConversion) )
      return error_handler(RANGECHECK) ;
    break ;

  case NAME_CharScanConversion:
    if ( !scanconvert_from_name(theo, 0 /*disallow*/,
                                &userparams->CharScanConversion) )
      return error_handler(RANGECHECK) ;
    break ;

  case NAME_PCLScanConversion:
    if ( !scanconvert_from_name(theo,
                                (1 << SC_RULE_TESSELATE) /*disallow*/,
                                &userparams->PCLScanConversion) )
      return error_handler(RANGECHECK) ;
    break ;

  case NAME_OverridePatternTilingType:
    theval = oInteger(*theo);
    if  ( theval < -1 || theval > 3 )
      return error_handler(RANGECHECK);
    userparams->OverridePatternTilingType = CAST_SIGNED_TO_INT8(theval);
    break;

  case NAME_AlternateJPEGImplementations:
    userparams->AlternateJPEGImplementations = (int8)oBool(*theo) ;
    break;

  case NAMES_COUNTED: /* Finaliser */
    if ( smoothness_limit != NO_SMOOTHNESS_LIMIT ) {
      if ( (smoothness_limit & MIN_SMOOTHNESS_LIMIT) == 0 )
        minsmoothness = userparams->MinSmoothness ;
      if ( (smoothness_limit & MAX_SMOOTHNESS_LIMIT) == 0 )
        maxsmoothness = userparams->MaxSmoothness ;

      smoothness_limit = NO_SMOOTHNESS_LIMIT ;

      if ( minsmoothness < 0.0f ||
           maxsmoothness > 1.0f ||
           minsmoothness > maxsmoothness )
        return error_handler(RANGECHECK) ;

      userparams->MinSmoothness = minsmoothness ;
      userparams->MaxSmoothness = maxsmoothness ;
    }
    break ;

  } /* end switch */

  return TRUE ;
}




/* ----------------------------------------------------------------------------
   function:            currentuserparams author:              Luke Tunmer
   creation date:       18-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool ps_get_userparam(corecontext_t *context, uint16 nameid, OBJECT *result)
{
  USERPARAMS *userparams = context->userparams ;
  HQASSERT(result, "No object for userparam result") ;

  switch (nameid) {

  case NAME_MaxFontItem:
    object_store_integer(result, userparams->MaxFontItem) ;
    break;

  case NAME_MinFontCompress:
    object_store_integer(result, userparams->MinFontCompress) ;
    break;

  case NAME_MaxUPathItem:
    object_store_integer(result, userparams->MaxUPathItem) ;
    break;

  case NAME_MaxFormItem:
    object_store_integer(result, userparams->MaxFormItem) ;
    break;

  case NAME_MaxPatternItem:
    object_store_integer(result, userparams->MaxPatternItem) ;
    break;

  case NAME_MaxScreenItem:
    object_store_integer(result, userparams->MaxScreenItem) ;
    break;

  case NAME_MaxOpStack:
    object_store_integer(result, userparams->MaxOpStack) ;
    break;

  case NAME_MaxDictStack:
    object_store_integer(result, userparams->MaxDictStack) ;
    break;

  case NAME_MaxExecStack:
    object_store_integer(result, userparams->MaxExecStack) ;
    break;

  case NAME_MaxLocalVM:
    object_store_integer(result, userparams->MaxLocalVM) ;
    break;

  case NAME_VMReclaim:
    object_store_integer(result, userparams->VMReclaim) ;
    break;

  case NAME_VMThreshold:
    object_store_integer(result, userparams->VMThreshold) ;
    break;

  case NAME_ImpositionForceErasePage:
    object_store_bool(result, userparams->ImpositionForceErasePage) ;
    break;

  case NAME_JobName:
    {
      OBJECT * statusdict_jobname;
      /* Now get the appropriate value from statusdict */
      statusdict_jobname = fast_extract_hash_name(&systemdict, NAME_statusdict);
      if (! statusdict_jobname)
        return error_handler (UNREGISTERED);
      statusdict_jobname = fast_extract_hash_name(statusdict_jobname, NAME_jobname);
      if (! statusdict_jobname)
        return error_handler (UNREGISTERED);

      if ( !ps_string(result, oString(*statusdict_jobname),
                      theLen(*statusdict_jobname)) )
        return error_handler( VMERROR ) ;
    }
    break;

  case NAME_JobTimeout:
    object_store_integer(result, curr_jobtimeout());
    break;

  case NAME_WaitTimeout:
    {
      OBJECT * statusdict_waittimeout;
      /* Now get the appropriate value from statusdict */
      statusdict_waittimeout = fast_extract_hash_name(&systemdict, NAME_statusdict);
      if (! statusdict_waittimeout)
        return error_handler (UNREGISTERED);
      statusdict_waittimeout = fast_extract_hash_name(statusdict_waittimeout, NAME_waittimeout);
      if (! statusdict_waittimeout)
        return error_handler (UNREGISTERED);

      OCopy(*result, *statusdict_waittimeout) ;
    }
    break;

  case NAME_ForceFontCompress:
    {
      int32 i ;
      static OBJECT forcefontcompressvalues[ MAX_FORCEFONTCOMPRESS ] ;
      for ( i = 0 ; i < MAX_FORCEFONTCOMPRESS ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        object_store_integer(object_slot_notvm(&forcefontcompressvalues[i]),
                             userparams->ForceFontCompress[i]) ;
      }

      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen( *result ) = MAX_FORCEFONTCOMPRESS ;
      oArray( *result ) = forcefontcompressvalues ;
    }
    break;

  case NAME_NeverRender:
    object_store_bool(result, userparams->NeverRender) ;
    break;

  case NAME_VignetteDetect:
    object_store_bool(result, userparams->VignetteDetect != VDL_None) ;
    break;

  case NAME_VignetteMinFills:
    object_store_integer(result, userparams->VignetteMinFills) ;
    break;

  case NAME_VignetteLinearMaxStep:
    object_store_real(result, userparams->VignetteLinearMaxStep) ;
    break;

  case NAME_VignetteLogMaxStep:
    object_store_real(result, userparams->VignetteLogMaxStep) ;
    break;

  case NAME_AdobeFilePosition:
    object_store_bool(result, userparams->AdobeFilePosition) ;
    break;

  case NAME_ShadingAntiAliasDefault:
    object_store_name(result, userparams->ShadingAntiAliasDefault.nameNumber,
                      LITERAL);
    break;

  case NAME_ResolutionFactor:
    if (userparams->ResolutionFactor > 0.0 ) {
      object_store_real(result, userparams->ResolutionFactor) ;
    } else {
      object_store_null(result) ;
    }
    break;

  case NAME_IdiomRecognition:
    object_store_bool(result, userparams->IdiomRecognition) ;
    break;

  case NAME_MinShadingSize:
    object_store_real(result, userparams->MinShadingSize) ;
    break;

  case NAME_GouraudLinearity:
    object_store_integer(result, userparams->GouraudLinearity) ;
    break;

  case NAME_BlendLinearity:
    object_store_integer(result, userparams->BlendLinearity) ;
    break;

  case NAME_HalftoneMode:
    object_store_integer(result, userparams->HalftoneMode) ;
    break;

  case NAME_MaxSuperScreen:
    object_store_integer(result, userparams->MaxSuperScreen) ;
    break;

  case NAME_RejectPreseparatedJobs:
    object_store_bool(result, userparams->RejectPreseparatedJobs) ;
    break;

  case NAME_AutomaticBinding:
    object_store_bool(result, userparams->AutomaticBinding) ;
    break;

  case NAME_AccurateScreens:
    object_store_bool(result, userparams->AccurateScreens) ;
    break;

  case NAME_AdobeArct:
    object_store_bool(result, userparams->AdobeArct) ;
    break;

  case NAME_RecombineWeakMergeTest:
    object_store_bool(result, userparams->RecombineWeakMergeTest) ;
    break;

  case NAME_RecombineTrapWidth:
    object_store_real(result, userparams->RecombineTrapWidth) ;
    break;

  case NAME_RecombineObject:
    object_store_integer(result, userparams->RecombineObject);
    break;

  case NAME_RecombineObjectThreshold:
    object_store_integer(result, userparams->RecombineObjectThreshold);
    break;

  case NAME_RecombineObjectProportion:
    object_store_real(result, userparams->RecombineObjectProportion);
    break;

  case NAME_BackdropReserveSize:
    object_store_integer(result, userparams->BackdropReserveSize);
    break;

  case NAME_BackdropResourceLimit:
    object_store_integer(result, userparams->BackdropResourceLimit);
    break;

  case NAME_UseScreenCacheName:
    object_store_bool(result, userparams->UseScreenCacheName) ;
    break;

  case NAME_SilentFontFault:
    object_store_bool(result, userparams->SilentFontFault) ;
    break;

  case NAME_InterpolateAllImages:
    object_store_name(result, userparams->InterpolateAllImages.nameNumber,
                      LITERAL) ;
    break;

  case NAME_InterpolateAllMasks:
    object_store_name(result, userparams->InterpolateAllMasks.nameNumber,
                      LITERAL) ;
    break;

  case NAME_MinSmoothness:
    object_store_real(result, userparams->MinSmoothness) ;
    break ;

  case NAME_MaxSmoothness:
    object_store_real(result, userparams->MaxSmoothness) ;
    break ;

  case NAME_SnapImageCorners:
    object_store_bool(result, userparams->SnapImageCorners) ;
    break;

  case NAME_PatternOverprintOverride:
    object_store_bool(result, userparams->PatternOverprintOverride) ;
    break;

  case NAME_MaxGouraudDecompose:
    object_store_integer(result, userparams->MaxGouraudDecompose) ;
    break;

  case NAME_EnablePseudoErasePage:
    object_store_bool(result, userparams->EnablePseudoErasePage) ;
    break;

  case NAME_RetainedRasterCompressionLevel:
    object_store_integer(result, userparams->RetainedRasterCompressionLevel) ;
    break;

  case NAME_ImageDecimation:
    Copy(result, &userparams->decimation.paramDict);
    break;

  case NAME_ImageDownsampling:
    object_store_bool(result, userparams->ImageDownsampling) ;
    break ;

  case NAME_OverridePatternTilingType:
    object_store_integer(result, userparams->OverridePatternTilingType);
    break;

  case NAME_AlternateJPEGImplementations:
    object_store_bool(result, userparams->AlternateJPEGImplementations);
    break;
  } /* end of switch */

  return TRUE;
}

/* ---------------------------------------------------------------------- */
Bool currentuserparam_(ps_context_t *pscontext)
{
  return currentparam(pscontext, pscontext->corecontext->userparamlist) ;
}

/* ---------------------------------------------------------------------- */
Bool currentuserparams_(ps_context_t *pscontext)
{
  return currentparams(pscontext, pscontext->corecontext->userparamlist) ;
}


/* ---------------------------------------------------------------------- */
/** GC root for objects in \c UserParams. */
static mps_root_t UserParamsRoot;

/** GC scanning function for \c UserParams. */
mps_res_t MPS_CALL scanUserParams(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res;
  USERPARAMS *userparams = p ;

  UNUSED_PARAM( size_t, s );
  res = ps_scan_field( ss, &userparams->decimation.paramDict );
  if (res != MPS_RES_OK) return res;

  return MPS_RES_OK;
}


/* ---------------------------------------------------------------------- */
static module_params_t ps_user_params = {
  user_match,
  ps_set_userparam,
  ps_get_userparam,
  NULL
} ;

static Bool userparams_swinit(struct SWSTART *params)
{
  int32 i;
  corecontext_t *context = get_core_context() ;
  USERPARAMS *userparams ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  context->userparams = userparams = mm_alloc_static(sizeof(USERPARAMS)) ;
  if ( userparams == NULL )
    return FALSE ;

  /* Link accessors into global list */
  HQASSERT(ps_user_params.next == NULL,
           "Already linked user params accessor") ;
  ps_user_params.next = context->userparamlist ;
  context->userparamlist = &ps_user_params ;

  /* The following three are overridden by setDynamicGlobalDefaults() */
  userparams->MaxFontItem = UPPER_CACHE ;
  userparams->MinFontCompress = LOWER_CACHE ;
  userparams->MaxUPathItem = 0 ;

  userparams->MaxFormItem = 0 ;
  userparams->MaxPatternItem = 0 ;
  userparams->MaxScreenItem = 0 ;
  userparams->MaxOpStack = operandstack.limit = 65536 ;
  userparams->MaxDictStack = dictstack.limit = 65536 ;
  userparams->MaxExecStack = executionstack.limit = 65536 ;
  userparams->MaxLocalVM = MAXINT32;
  userparams->VMReclaim = 0 ; /* local and global enabled */
  userparams->VMThreshold = DEFAULTVMTHRESHOLD ;

  userparams->AutomaticBinding = TRUE;
  userparams->ImpositionForceErasePage = FALSE;
  userparams->SnapImageCorners = FALSE;
  userparams->InterpolateAllImages.nameNumber = NAME_ForceFalse;
  userparams->InterpolateAllMasks.nameNumber = NAME_ForceFalse;

  for ( i = 0 ; i < MAX_FORCEFONTCOMPRESS ; ++i )
    userparams->ForceFontCompress[ i ] = 100 ;   /* Percentage */

  userparams->NeverRender = FALSE;
  userparams->VignetteDetect = VDL_None;
  userparams->AdobeFilePosition = FALSE;

  userparams->VignetteMinFills = 6;
  userparams->VignetteLinearMaxStep = 0.1f;
  userparams->VignetteLogMaxStep = 0.1f;

  userparams->IdiomRecognition = TRUE ;
  userparams->RejectPreseparatedJobs = FALSE;
  userparams->ResolutionFactor = -1.0f ;
  userparams->HalftoneMode = 0;
  userparams->AccurateScreens = FALSE ;

  /* Default for arct(o) is not Adobe compatible */
  userparams->AdobeArct = FALSE;

  userparams->ShadingAntiAliasDefault.nameNumber = NAME_DefaultFalse;  /* Default as per PLRM.3 */
  userparams->ShadingAntiAliasFactor = 1.0f;
  userparams->ShadingAntiAliasSize = 0.72f;

  userparams->MinShadingSize = 1.0f ;
  userparams->GouraudLinearity = 2 ;
  userparams->MaxGouraudDecompose = 32 ;
  userparams->BlendLinearity = 1 ;

  userparams->MaxSuperScreen = 1016;

  userparams->RecombineWeakMergeTest = FALSE;
  /* 1.0 seems a reasonable value to pluck from the air (0.144 is the
     default trap width set by Quark).  The reason for this param is
     to avoid false positive recombine object matches. */
  userparams->RecombineTrapWidth = 1.0f;

  /* RecombineObject:
     0 - Completely disable object recombine.
     1 - Enable object recombine for all objects, subject to dynamic control
         (see RecombineObjectThreshold and RecombineObjectProportion below).
     2 - Enable object recombine solely for text (100% black overpinting). */
  userparams->RecombineObject = 1;

  /* Params to control the behaviour of recombine object matching.  Once the
     number of preseparated objects exceeds RecombineObjectThreshold, object
     matching is attempted only if the proportion of objects merged exceeds
     RecombineObjectProportion.*/
  userparams->RecombineObjectThreshold = 5000;
  userparams->RecombineObjectProportion = 0.98f;


/* BACKDROP_RESERVE_DEFAULT is the default value for the system param
   BackdropReserveSize which specifies the amount of memory which should be left
   after allocating backdrop resources.  The actual size left in reserve for
   color conversion may be over or under depending on the minimum/maximum number
   of resources required.  If each group created and retained an appropriate
   color chain to convert from blend space to parent space and if compositing
   was performed in a separeate phase then this reserve may be removed. */
#define BACKDROP_RESERVE_DEFAULT (0x10000) /* bytes */
  userparams->BackdropReserveSize = BACKDROP_RESERVE_DEFAULT;

/* Limit the number of backdrop resources used by a page to this number if
   possible. The actual number used may be larger if this value is less than the
   minimum required. */
#define BACKDROP_RESOURCE_LIMIT_DEFAULT (250)
  userparams->BackdropResourceLimit = BACKDROP_RESOURCE_LIMIT_DEFAULT;

  userparams->UseScreenCacheName = FALSE;
  userparams->SilentFontFault = FALSE;

#define HQN_MIN_SMOOTHNESS (0.000244140625f) /* aka 1/4096 */
#define ADB_MIN_SMOOTHNESS_3010_103 (0.005)  /* aka 0.5%   */
#define ADB_MIN_SMOOTHNESS_3010_106 (0.002)  /* aka 0.2%   */
  userparams->MinSmoothness = HQN_MIN_SMOOTHNESS;
  userparams->MaxSmoothness = 1.0f;



  userparams->PatternOverprintOverride = TRUE;
  userparams->EnablePseudoErasePage = FALSE;
  userparams->StrokeScanConversion = SC_RULE_HARLEQUIN;
  userparams->CharScanConversion = SC_RULE_HARLEQUIN;
  userparams->PCLScanConversion = SC_RULE_HARLEQUIN;
  userparams->OverridePatternTilingType = -1 ;

  userparams->RetainedRasterCompressionLevel = 1 ;

  userparams->decimation.paramDict = onull; /* Struct copy to set slot properties */
  userparams->decimation.enabled = FALSE;
  userparams->decimation.minimumArea = (100 * 100);
  userparams->decimation.minimumResolutionPercentage = 50;

  userparams->ImageDownsampling = FALSE;

  userparams->AlternateJPEGImplementations = TRUE;

  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &UserParamsRoot, mm_arena, mps_rank_exact(),
                        0, scanUserParams, userparams, 0 ) != MPS_RES_OK )
    return FAILURE(FALSE) ;

  return TRUE ;
}


/** Deinitialization for \c UserParams. */
static void userparams_finish(void)
{
  corecontext_t *context = get_core_context() ;
  USERPARAMS *userparams;

  if (context && (userparams = context->userparams) != NULL) {
    userparams->decimation.paramDict = onull; /* Struct copy to set slot properties */
  }
  mps_root_destroy( UserParamsRoot );
}

void userparams_C_globals(core_init_fns *fns)
{
  ps_user_params.next = NULL ;
  UserParamsRoot = NULL ;
  fns->swinit = userparams_swinit ;
  fns->finish = userparams_finish ;
}


/* ---------------------------------------------------------------------- */
static NAMETYPEMATCH system_match[] = {
  { NAME_SystemParamsPassword | OOPTIONAL, 2,
                { OSTRING , OINTEGER }},
  { NAME_StartJobPassword | OOPTIONAL, 2,
                { OSTRING , OINTEGER }},
  { NAME_BuildTime | OOPTIONAL, 1, { OINTEGER }},
  { NAME_BuildDate | OOPTIONAL, 1, { OSTRING }},
  { NAME_SourceDate | OOPTIONAL, 1, { OSTRING }},
  { NAME_ByteOrder | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_RealFormat | OOPTIONAL, 1, { OSTRING }},
  { NAME_MaxOutlineCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurOutlineCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxUPathCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurUPathCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxFormCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurFormCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxPatternCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurPatternCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxScreenStorage | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurScreenStorage | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxDisplayList | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurDisplayList | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CurSourceList | OOPTIONAL, 1, { OINTEGER }},
  { NAME_LanguageLevel | OOPTIONAL, 1, { OINTEGER }},
  { NAME_ImagemaskBug | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_CompositeFonts | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ColorExtension | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ParseComments | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AutoShowpage | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AutoPrepLoad | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_PoorPattern | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AccurateScreens | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ScreenRotate | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ScreenWithinTolerance | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ScreenExtraGrays | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ScreenDotCentered | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ScreenAngleSnap | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OverrideSpotFunction | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CompressBands | OOPTIONAL, 1, { OINTEGER }},
  { NAME_OverrideFrequency | OOPTIONAL, 2, { OREAL,OINTEGER }},
  { NAME_ScreenZeroAdjust | OOPTIONAL, 2, { OREAL,OINTEGER }},
  { NAME_ScreenAngleAccuracy | OOPTIONAL, 2,
                { OREAL,OINTEGER }},
  { NAME_ScreenFrequencyAccuracy | OOPTIONAL, 2,
                { OREAL,OINTEGER }},
  { NAME_ScreenFrequencyDeviation | OOPTIONAL, 2,
                { OREAL,OINTEGER }},
  { NAME_MaxGsaves | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CountLines | OOPTIONAL , 1 , { OBOOLEAN }},
  { NAME_PoorStrokepath | OOPTIONAL , 1 , { OBOOLEAN }},
  { NAME_Level1ExpandDict | OOPTIONAL , 1 , { OBOOLEAN }},
  { NAME_PreAllocation | OOPTIONAL , 2 , { OINTEGER , OREAL }},
  { NAME_ScreenLevels | OOPTIONAL , 3 ,
                { OINTEGER , OARRAY, OPACKEDARRAY }},
  { NAME_DynamicBands | OOPTIONAL , 1 , {OBOOLEAN}},
  { NAME_DynamicBandLimit | OOPTIONAL , 1 , {OINTEGER}},
  { NAME_MaxBandMemory | OOPTIONAL, 1, {OREAL}},
  { NAME_BaseMapSize | OOPTIONAL , 1 , {OINTEGER}},
  { NAME_DisplayListUsed | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_ScreenAngles | OOPTIONAL, 2,
                { OARRAY, OPACKEDARRAY }},
  { NAME_AsyncMemorySize | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_MaxScreenTable | OOPTIONAL, 1, {OINTEGER}},
  { NAME_CacheNewSpotFunctions | OOPTIONAL, 1, {OBOOLEAN}},
  { NAME_ScreenZeroFromRequest | OOPTIONAL , 1 , { OBOOLEAN }},
  { NAME_CurInputDevice | OOPTIONAL, 1, { OSTRING }},
  { NAME_CurOutputDevice | OOPTIONAL, 1, { OSTRING }},
  { NAME_OverrideSpotFunctionName | OOPTIONAL, 3,
                { OSTRING, ONAME, ONULL }},
  { NAME_Revision | OOPTIONAL, 1, { OINTEGER }},
  { NAME_FontResourceDir | OOPTIONAL, 1, { OSTRING }},
  { NAME_GenericResourceDir | OOPTIONAL, 1, { OSTRING }},
  { NAME_GenericResourcePathSep | OOPTIONAL, 1, { OSTRING }},
  { NAME_JobTimeout | OOPTIONAL, 1, { OINTEGER }},
  { NAME_LicenseID | OOPTIONAL, 1, { OSTRING }},
  { NAME_DemoRIP | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_WaitTimeout | OOPTIONAL, 1, { OINTEGER }},
  { NAME_OverrideAngle | OOPTIONAL, 2, { OREAL,OINTEGER }},
  { NAME_RelevantRepro | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_AdobeSetHalftone | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_HalftoneRetention | OOPTIONAL, 1, { OINTEGER }},
  { NAME_SeparationMethod | OOPTIONAL, 1, { OSTRING }},
  { NAME_Separation | OOPTIONAL, 2, { ONAME, OSTRING }},
  { NAME_MaxImagePixel | OOPTIONAL, 1, { OINTEGER }},
  { NAME_DetectSeparation | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_HDS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HMS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HCS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_MaxResolution | OOPTIONAL, 1, { OINTEGER }},
  { NAME_ProductName | OOPTIONAL, 1, { OSTRING }},
  { NAME_PoorClippath | OOPTIONAL, 2,
                { OARRAY, OPACKEDARRAY }},
  { NAME_RangeCheckPath | OOPTIONAL, 1, {OBOOLEAN}},
  { NAME_RamSize | OOPTIONAL, 1, {OINTEGER}},
  { NAME_MinScreenDetected | OOPTIONAL , 2 ,
                { OINTEGER , OREAL }} ,
  { NAME_HPS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HXM | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HXMLOWRES | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_RevisionPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_DLMS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_ForceStrokeAdjust | OOPTIONAL, 2 , {OBOOLEAN, ONAME}},
  { NAME_ForceRectWidth | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_MinLineWidth | OOPTIONAL, 3,
                { OINTEGER , OARRAY, OPACKEDARRAY }},
  { NAME_TrapProLite | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_PDFOutPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_TIFF6 | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_TIFFIT | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HDSLOWRES | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_AccurateRenderThreshold | OOPTIONAL, 3,
    { OBOOLEAN, OINTEGER, ONULL }},
  { NAME_AccurateTwoPassThreshold | OOPTIONAL, 3,
    { OBOOLEAN, OINTEGER, ONULL }},
  { NAME_Type1StemSnap | OOPTIONAL, 3, { OBOOLEAN, OINTEGER, OREAL }},
  { NAME_DefaultImageFileCache | OOPTIONAL, 1, { OINTEGER }},
  { NAME_DetectScreenAngles | OOPTIONAL, 3,
                { ONULL, OARRAY, OPACKEDARRAY }},
  { NAME_AdobeSetFlat | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_PoorFlattenpath | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_EnableStroker | OOPTIONAL, 2 ,
                { OARRAY, OPACKEDARRAY }},
  { NAME_AdobeSetLineJoin | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_HPSTwo | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_LowMemImagePurgeToDisk | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_PurgeImageSource | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_TickleWarnTime | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_PostScriptPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_PDFPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_XPSPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_ApplyWatermarkPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_CompressImageSource | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_MaxDisplayAndSourceList | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxSourceList | OOPTIONAL, 1, { OINTEGER }},
  { NAME_MaxInterpreterLevel | OOPTIONAL, 1, { OINTEGER }},
  { NAME_CompressImageParms | OOPTIONAL, 1, { ODICTIONARY }},
  { NAME_Picture | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_ICC | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HCMS | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HCMSLite | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_HCEP | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_DeviceImageOptimization | OOPTIONAL, 1 , {OBOOLEAN}},
  { NAME_TransparencyStrategy | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_TrapPro | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_PlatformPassword | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_EncryptScreens | OOPTIONAL, 1 , {OINTEGER}},
  { NAME_OSLocale | OOPTIONAL, 1, { OSTRING }},
  { NAME_RIPLocale | OOPTIONAL, 1, { OSTRING }},
  { NAME_OperatingSystem | OOPTIONAL, 1, { OSTRING }},
  { NAME_StripeImages | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_OptimizeClippedImages | OOPTIONAL, 1, { OBOOLEAN }},
  { NAME_Rainstorm | OOPTIONAL, 1, { OINTEGER }},
  { NAME_DLBanding | OOPTIONAL, 1, { OINTEGER }},
  { NAME_NumDisplayLists | OOPTIONAL, 1, { OINTEGER }},
  { NAME_Pipelining | OOPTIONAL, 1, { OINTEGER }},
  { NAME_HVDExternal | OOPTIONAL, 1, { OINTEGER }},
  { NAME_HVDInternal | OOPTIONAL, 1, { OINTEGER }},
  { NAME_PoorShading | OOPTIONAL, 1, { OBOOLEAN }},
  DUMMY_END_MATCH
  };

/* ----------------------------------------------------------------------------
   function:            setsystemparams_  author:              Luke Tunmer
   creation date:       18-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool setsystemparams_(ps_context_t *pscontext)
{
  corecontext_t *context = pscontext->corecontext ;
  SYSTEMPARAMS *systemparams = context->systemparams ;
  OBJECT *thed ;

  /* If that last number becomes >= SYSPARAMDICT, then make SYSPARAMDICT
   * bigger to accomodate it. */

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  thed = theTop( operandstack ) ;

  if ( oType(*thed) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  if ( ! oCanRead(*oDict(*thed)) )
    if ( ! object_access_override(oDict(*thed)))
      return error_handler(INVALIDACCESS) ;

  /* Check for passwords only if the system password is set */
  if ( systemparams->SystemPasswordLen != 0 ) {
    OBJECT *theo ;

    oName(nnewobj) = &system_names[ NAME_Password ] ;
    if ( NULL == ( theo = extract_hash( thed , &nnewobj )))
      return error_handler( INVALIDACCESS ) ;
    if ( ! check_sys_password(context, theo) )
      return error_handler( INVALIDACCESS ) ;
  }

  if ( !setparams(context, thed, context->systemparamlist) )
    return FALSE ;

  pop(& operandstack);
  return TRUE ;
}

/*
 * Routines to independently handle image block compression for different bit
 * depths, using CompressImageParm systemparam. The value for this system
 * param key is a dictionary, containing key-value pairs for each bit depth we
 * wish to specify (key is bit depth, as integer; value is boolean, indicating
 * whether or not to compress at that bit depth). This param works in
 * conjunction with CompressImageSource such that if CompressImageSource is
 * false then no image block compression is performed, otherwise compression
 * as sprecified by CompressImageParms is performed.
 *
 * The routines below actually unpack the dictionary into a bit field to
 * reduce the overhead of bit-depth compression test, or pack that bit field
 * back into a dictionary (for reporting by currentsystemparams)
 *
 * Note that 12bpp images are generally stored internally using 16bpp
 * containers (ie ims->bpp == 16) so we won't necessarilly do what the
 * user expected for "/CompressImageParms << 12 true >>"
 */

static int32 cipBpps[] = { 1, 2, 4, 8, 12, 16, 32};

/**
 * Unpack the image compression options specified in the dictionary
 * into a set of bit flags.
 * \param[in]  cipdict  Dictionary specifying image compression options
 * \param[out] cipflags Resulting bit flags
 * \return              Success status
 */
static Bool cipUnpack(OBJECT *cipdict, uint32 *cipflags)
{
  int32 idx, max;
  uint32 flags;
  OBJECT *theo, thek = OBJECT_NOTVM_NOTHING;

  flags = *cipflags;
  max = sizeof(cipBpps)/sizeof(cipBpps[0]);

  theTags(thek) = OINTEGER|LITERAL;
  for ( idx = 0; idx < max; idx++ )
  {
    oInteger(thek) = cipBpps[idx];
    theo = extract_hash(cipdict, &(thek));
    if ( theo != NULL )
    {
      uint32 val, mask;
      int32  shift;

      if ( oType(*theo) != OBOOLEAN )
        return error_handler(TYPECHECK);
      val = (oBool(*theo) ? 1u : 0u);
      shift = cipBpps[idx] - 1;
      val <<= shift;
      mask = 1u << shift;
      flags = (flags & ~mask)|val;
    }
  }
  *cipflags = flags;
  return TRUE;
}

/**
 * Re-pack the image compression bits into the given dictionary
 * \param[in]   cipflags Input image compression bit flags
 * \param[out]  cipdict  Output dictionary
 * \return              Success status
 */
static Bool cipPack(int32 cipflags, OBJECT *cipdict)
{
  int32 idx, max;
  OBJECT thek = OBJECT_NOTVM_NOTHING;

  max = NUM_ARRAY_ITEMS(cipBpps);

  if ( !ps_dictionary(cipdict, max) )
    return FALSE;

  theTags(thek) = OINTEGER|LITERAL;
  for ( idx = 0; idx < max; idx++ )
  {
    oInteger(thek) = cipBpps[idx];
    if ( cipflags & (uint32)(1 << (cipBpps[idx] - 1)) )
    {
      if ( !insert_hash(cipdict, &thek, &tnewobj) )
        return FALSE;
    }
    else
    {
      if ( !insert_hash(cipdict, &thek, &fnewobj) )
        return FALSE;
    }
  }
  return TRUE;
}

Bool ps_set_systemparam(corecontext_t *context, uint16 name, OBJECT *theo)
{
  SYSTEMPARAMS *systemparams = context->systemparams ;
  int32 theval ;
  USERVALUE thefloat ;

  static Bool redoscreens = FALSE ;

  HQASSERT((theo && name < NAMES_COUNTED) ||
           (!theo && name == NAMES_COUNTED),
           "name and parameter object inconsistent") ;

  switch ( name ) {

  case NAME_SystemParamsPassword:
    if ( ! set_sys_password(systemparams, theo) )
      return FALSE ;
    break ;

  case NAME_StartJobPassword:
    if ( ! set_job_password(context, theo) )
      return FALSE ;
    break ;

  /* Cannot set BuildTime */

  /* Cannot set BuildDate */

  /* Cannot set SourceDate */

  /* Cannot set ByteOrder */

  /* Cannot set RealFormat */

  case NAME_MaxOutlineCache:
    systemparams->MaxOutlineCache = oInteger(*theo) ;
    break ;

  /* Cannot set CurOutlineCache */

  case NAME_MaxUPathCache:
    systemparams->MaxUPathCache = oInteger(*theo) ;
    break ;

  /* Cannot set CurUPathCache */

  case NAME_MaxFormCache:
    systemparams->MaxFormCache = oInteger(*theo) ;
    break ;

  /* Cannot set CurFormCache */

  case NAME_MaxPatternCache:
    systemparams->MaxPatternCache = oInteger(*theo) ;
    break ;

  /* Cannot set CurPatternCache */

  case NAME_MaxScreenStorage:
    systemparams->MaxScreenStorage = oInteger(*theo) ;
    break ;

  /* Cannot set CurScreenStorage */

  case NAME_MaxDisplayList:
    systemparams->MaxDisplayList = oInteger(*theo) ;
    break ;

  /* Cannot set CurDisplayList */

  /* Cannot set CurSourceList */

  case NAME_LanguageLevel:
    theval = oInteger(*theo) ;
    if (( theval != 1 ) && ( theval != 2 ) && ( theval != 3 )) {
      OBJECT nameobj = OBJECT_NOTVM_NAME(NAME_LanguageLevel, LITERAL) ;
      return errorinfo_error_handler( CONFIGURATIONERROR , &nameobj , theo ) ;
    }
    systemparams->LanguageLevel = ( uint8 )theval ;
    break ;

  case NAME_ImagemaskBug:
    systemparams->ImagemaskBug = (uint8)oBool(*theo) ;
    break ;

  case NAME_CompositeFonts:
    systemparams->CompositeFonts = (uint8)oBool(*theo) ;
    break ;

  case NAME_ColorExtension:
    systemparams->ColorExtension = (uint8)oBool(*theo) ;
    break ;

  case NAME_ParseComments:
    systemparams->ParseComments = (uint8)oBool(*theo) ;
    break ;

  case NAME_AutoShowpage:
    systemparams->AutoShowpage = (uint8)oBool(*theo) ;
    break ;

  case NAME_AutoPrepLoad:
    systemparams->AutoPrepLoad = (uint8)oBool(*theo) ;
    break ;

  case NAME_PoorPattern:
    systemparams->PoorPattern = (uint8)oBool(*theo) ;
    break ;

  case NAME_AccurateScreens:
    systemparams->AccurateScreens = (uint8)oBool(*theo) ;
    if (DongleHPSDenied() && !systemparams->HPS) {
      systemparams->AccurateScreens = FALSE;
    }
    else if (RIP_DenyFeature(RIPFEATURE_HPS)) {
      systemparams->AccurateScreens = FALSE;
    }
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenRotate:
    systemparams->ScreenRotate = (uint8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenWithinTolerance:
    systemparams->ScreenWithinTolerance = (uint8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenExtraGrays:
    systemparams->ScreenExtraGrays = (uint8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenDotCentered:
    systemparams->ScreenDotCentered = (uint8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenAngleSnap:
    systemparams->ScreenAngleSnap = (uint8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_OverrideSpotFunction:
    systemparams->OverrideSpotFunction = (int8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_CompressBands:
    systemparams->CompressBands = (int8)oInteger(*theo) ;
    break ;

  case NAME_OverrideFrequency:
    if ( oType(*theo) == OINTEGER )
      systemparams->OverrideFrequency = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->OverrideFrequency = oReal(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenZeroAdjust:
    if ( oType(*theo) == OINTEGER )
      systemparams->ScreenZeroAdjust = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->ScreenZeroAdjust = oReal(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenAngleAccuracy:
    if ( oType(*theo) == OINTEGER )
      systemparams->ScreenAngleAccuracy = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->ScreenAngleAccuracy = oReal(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenFrequencyAccuracy:
    if ( oType(*theo) == OINTEGER )
      systemparams->ScreenFrequencyAccuracy = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->ScreenFrequencyAccuracy = oReal(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_ScreenFrequencyDeviation:
    if ( oType(*theo) == OINTEGER )
      systemparams->ScreenFrequencyDeviation = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->ScreenFrequencyDeviation = oReal(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_MaxGsaves:
    theval = oInteger(*theo) ;
    if ( theval < 0 )
      return error_handler( RANGECHECK ) ;
    systemparams->MaxGsaves = theval ;
    break ;

  case NAME_CountLines:
    systemparams->CountLines = (uint8)oBool(*theo) ;
    break ;

  case NAME_PoorStrokepath:
    systemparams->PoorStrokepath = (int8)oBool(*theo) ;
    break ;

  case NAME_Level1ExpandDict:
    systemparams->Level1ExpandDict = (int8)oBool(*theo) ;
    break ;

  case NAME_PreAllocation:
    if ( oType(*theo) == OINTEGER )
          thefloat = (USERVALUE)oInteger(*theo) ;
        else
          thefloat = oReal(*theo) ;
    if ( thefloat <= 0.0 )
      return error_handler( RANGECHECK ) ;
    systemparams->PreAllocation = thefloat ;
    break ;

  case NAME_ScreenLevels:
    {
      int32 screenvals[ SLS_ELEMENTS ] ;
      if ( oType(*theo) == OINTEGER )
        screenvals[ 0 ] = screenvals[ 1 ] = oInteger(*theo) ;
      else {
        int32 i ;
        OBJECT *olist ;

        /* zero the screen vals to start with. This means leave unaffected. */
        for ( i = 0 ; i < SLS_ELEMENTS ; ++i )
          screenvals[ i ] = 0 ;

        olist = oArray(*theo) ;
        for ( i = 0 ; i < SLS_ELEMENTS && i < theLen(*theo) ; ++i )
          if ( oType( olist[ i ] ) == OINTEGER )
            screenvals[ i ] = oInteger(olist[ i ]) ;
          else
            return error_handler( RANGECHECK ) ;
      }
      if ( screenvals[ 0 ] < 0 || screenvals[ 0 ] > COLORVALUE_MAX ||
           screenvals[ 1 ] < 0 || screenvals[ 1 ] > 65536 )
        /* Allow 65536, though the code will never give more than COLORVALUE_MAX
           levels, because all our old PSUs set that. */
        return error_handler( RANGECHECK ) ;

      if ( screenvals[ 0 ] > 0 ) systemparams->GrayLevels   = screenvals[ 0 ] ;
      if ( screenvals[ 1 ] > 0 ) systemparams->ScreenLevels = screenvals[ 1 ] ;
      if ( systemparams->GrayLevels > systemparams->ScreenLevels )
        systemparams->ScreenLevels = systemparams->GrayLevels ;
      redoscreens = TRUE ;
    }
    break ;

  case NAME_DynamicBands:
    systemparams->DynamicBands = (int8)oBool(*theo) ;
    break ;

  case NAME_DynamicBandLimit:
    systemparams->DynamicBandLimit = oInteger(*theo) ;
    break ;

  case NAME_MaxBandMemory: {
    USERVALUE mem = oReal(*theo);
    if ( mem < 0 )
      return error_handler( RANGECHECK );
    systemparams->MaxBandMemory = mem;
    }
    break;

  case NAME_BaseMapSize:
    theval = oInteger(*theo);
    if ( theval <= 0 )
      return error_handler( RANGECHECK );
    systemparams->BaseMapSize = theval;
    break;

  case NAME_DisplayListUsed:
    theval = oInteger(*theo) ;
    if ( theval < 0 )
      return error_handler( RANGECHECK ) ;
    break ;

  case NAME_ScreenAngles:
    /* a bit more complex than most - an array of 4 numbers */
    if ( theLen(*theo) != 4)
      return error_handler (RANGECHECK);

    {
      int32 count = 4;
      SYSTEMVALUE arg[4] ;

      theo = oArray(*theo);
      while (--count >= 0) {
        if (! object_get_numeric(theo + count, arg + count))
          return FALSE;
        if ( *(arg + count) < 0.0 || *(arg + count) >= 90.0 )
          return error_handler (RANGECHECK);
      }
      systemparams->ScreenAngles[0] = (USERVALUE) arg[0];
      systemparams->ScreenAngles[1] = (USERVALUE) arg[1];
      systemparams->ScreenAngles[2] = (USERVALUE) arg[2];
      systemparams->ScreenAngles[3] = (USERVALUE) arg[3];
    }
    redoscreens = TRUE ;
    break ;

  case NAME_AsyncMemorySize:
    theval = oInteger(*theo) ;
    if ( theval < 0 )
      return error_handler( RANGECHECK ) ;
    systemparams->AsyncMemorySize = theval ;
    init_async_memory(systemparams) ;
    break ;

  case NAME_MaxScreenTable:
    theval = oInteger(*theo) ;
    if ( theval < 0 )
      return error_handler( RANGECHECK ) ;
    systemparams->MaxScreenTable = theval / 4 ;
      /* the size refers to 2 16 bit tables, so divide by 4 to get
         size of table */
    redoscreens = TRUE ;
    break ;

  case NAME_CacheNewSpotFunctions:
    systemparams->CacheNewSpotFunctions = (uint8)oBool(*theo) ;
    break ;

  case NAME_ScreenZeroFromRequest:
    systemparams->ScreenZeroFromRequest = (int8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_CurInputDevice:
    {
      uint8 *clist = oString(*theo);
      int32 clen = theLen(*theo);
      int32 i, c;

      /* only change /CurrentInputDevice if we are in exitserver */
      if (! NOTINEXITSERVER(context)) {

        c = CurInputDeviceString[0] = clist[0];
        for (i = 1; i < clen; i++)
        {
          if ((c = CurInputDeviceString[i] = clist[i]) == '%')
            break;

          if (i >= (MAX_DEVICE_NAME_LENGTH -1))
          {
            HQFAIL("Source string too long for CurInputDeviceString");
            break;
          }
        }
        if (c != '%' && CurInputDeviceString[0] == '%')
        {
          CurInputDeviceString[i] = '%';
        }
        CurInputDeviceString[++i] = '\0';
        systemparams->CurInputDeviceLen = i;
      }
    }
    break ;

  case NAME_CurOutputDevice:
    {
      uint8 *clist = oString(*theo);
      int32 clen = theLen(*theo);
      int32 i, c;

      /* only change /CurrentOutputDevice if we are in exitserver */
      if (! NOTINEXITSERVER(context)) {
        i = 0;

        c = CurOutputDeviceString[0] = clist[0];
        for (i = 1; i < clen; i++)
        {
          if ((c = CurOutputDeviceString[i] = clist[i]) == '%')
            break;

          if (i >= (MAX_DEVICE_NAME_LENGTH -1))
          {
            HQFAIL("Source string too long for CurOutputDeviceString");
            break;
          }
        }
        if (c != '%' && CurOutputDeviceString[0] == '%')
        {
          CurOutputDeviceString[i] = '%';
        }
        CurOutputDeviceString[++i] = '\0';
        systemparams->CurOutputDeviceLen = i;
      }
    }
    break ;

  case NAME_OverrideSpotFunctionName:
    if ( ! set_override_sf_name(systemparams, theo) )
      return FALSE ;
    redoscreens = TRUE ;
    break ;

  /* Cannot set Revision */

  case NAME_FontResourceDir:
    {
      int32 len = theLen(*theo) ;
      if ( len + 1 > MAX_RESOURCE_DIR_LENGTH )
        return error_handler( LIMITCHECK ) ;
      systemparams->FontResourceDirLen = len ;
      HqMemMove( systemparams->FontResourceDir , oString(*theo) , len) ;
    }
    break ;

  case NAME_GenericResourceDir:
    {
      int32 len = theLen(*theo) ;
      if ( len + 1 > MAX_RESOURCE_DIR_LENGTH )
        return error_handler( LIMITCHECK ) ;
      systemparams->GenericResourceDirLen = len ;
      HqMemMove( systemparams->GenericResourceDir , oString(*theo) , len) ;
    }
    break ;

  case NAME_GenericResourcePathSep:
    {
      int32 len = theLen(*theo) ;
      if ( len + 1 > MAX_RESOURCE_DIR_LENGTH )
        return error_handler( LIMITCHECK ) ;
      systemparams->GenericResourcePathSepLen = len ;
      HqMemMove( systemparams->GenericResourcePathSep , oString(*theo) , len) ;
    }
    break ;

  case NAME_JobTimeout:
    {
      int32 new_timeout = oInteger(*theo) ;
      if (new_timeout >= 0) {
        if (new_timeout != 0 && new_timeout < 15)
          new_timeout = 15;
        jobtimeout = new_timeout;
      }
    }
    break ;

  /* Cannot set LicenseID */

  /* Cannot set DemoRIP */

  case NAME_WaitTimeout:
    {
      int32 new_timeout = oInteger(*theo) ;
      if (new_timeout >= 0)
        systemparams->WaitTimeout = new_timeout;
    }
    break ;

  case NAME_OverrideAngle:
    {
      register USERVALUE t ;
      if ( oType(*theo) == OINTEGER )
        t = (USERVALUE)oInteger(*theo) ;
      else
        t = oReal(*theo) ;
      if ( t != -1.0 && ( t < 0.0 || t >= 360.0 ))
        return error_handler( RANGECHECK ) ;
      systemparams->OverrideAngle = t ;
      redoscreens = TRUE ;
    }
    break ;

  case NAME_RelevantRepro:
    systemparams->RelevantRepro = (uint8)oBool(*theo) ;
    break ;

  case NAME_AdobeSetHalftone:
    systemparams->AdobeSetHalftone = (int8)oBool(*theo) ;
    break ;

  case NAME_HalftoneRetention:
    systemparams->HalftoneRetention = (int32)oInteger(*theo);
    break ;

  /* Cannot set SeparationMethod */

  case NAME_Separation: /* Quark calls this %%PlateColor: Black */
    {
      NAMECACHE *sepname;

      if ( oType(*theo) == OSTRING ) {
        sepname = cachename(oString(*theo), theLen(*theo));
        if ( sepname == NULL ) {
          return FALSE;
        }

      } else {
        sepname = oName(*theo);
      }

      if ( !detect_setsystemparam_separation(sepname, gstateptr->colorInfo) ) {
        return FALSE;
      }

      redoscreens = TRUE;
    }
    break ;

  case NAME_MaxImagePixel:
    systemparams->MaxImagePixel = oInteger(*theo) ;
    break ;

  case NAME_DetectSeparation:
    if ( systemparams->DetectSeparation != oBool(*theo) ) {
      gs_invalidateAllColorChains();
      systemparams->DetectSeparation = (uint8)oBool(*theo) ;
    }
    /* If we turn separation detection off, then clear out any accumulated color we detected */
    if ( ! systemparams->DetectSeparation ) {
      reset_setsystemparam_separation() ;
      if ( ! reset_separation_detection_on_new_page(gstateptr->colorInfo))
        return FALSE ;
    }
    redoscreens = TRUE ;
    break ;

  case NAME_HDS:
    if ( SWpermission(oInteger(*theo), GNKEY_FEATURE_HDS) ) {
      systemparams->HDS = (int8)PARAMS_HDS_HIGHRES;
    } else if ( systemparams->HDS == (int8)PARAMS_HDS_HIGHRES ) {
      systemparams->HDS = (int8)PARAMS_HDS_DISABLED;
    }
    break ;

  case NAME_HMS:
    systemparams->HMS = (int8)SWpermission(oInteger(*theo),
                                          GNKEY_FEATURE_HMS);
    break ;

  case NAME_HCS:
    systemparams->HCS = (int8)SWpermission(oInteger(*theo),
                                          GNKEY_FEATURE_HCS);
    break ;

  case NAME_HPS:
    systemparams->HPS = (int8)SWpermission(oInteger(*theo),
                                          GNKEY_FEATURE_HPS);
    break ;

  case NAME_HXM:
    if ( SWpermission(oInteger(*theo), GNKEY_FEATURE_HXM) ) {
      systemparams->HXM = (int8)PARAMS_HXM_HIGHRES;
    } else if ( systemparams->HXM == (int8)PARAMS_HXM_HIGHRES ) {
      systemparams->HXM = (int8)PARAMS_HXM_DISABLED;
    }
    break ;

  case NAME_HXMLOWRES:
    /* Allow low res HXM.
     * Don't change the param if high res is already on.
     */
    if ( PARAMS_HXM_HIGHRES != systemparams->HXM ) {
      if ( SWpermission(oInteger(*theo), GNKEY_FEATURE_HXMLOWRES) ) {
        systemparams->HXM = (int8)PARAMS_HXM_LOWRES;
      } else {
        systemparams->HXM = (int8)PARAMS_HXM_DISABLED;
      }
    }
    break ;

  /* Cannot set MaxResolution */

  /* Cannot set ProductName */

  case NAME_PoorClippath:
    {
      int32 i, len;
      Bool flags[PCP_ELEMENTS];

      len = theLen(*theo) ;
      if (len > PCP_ELEMENTS)
        return error_handler (RANGECHECK);
      theo = oArray(*theo);
      for ( i = 0 ; i < len ; i++, theo++ ) {
        if ( oType(*theo) != OBOOLEAN)
          return error_handler(TYPECHECK);
        flags[i] = oBool(*theo) ;
      }
      for ( i = 0 ; i < len ; i++ ) {
        systemparams->PoorClippath[i] = (int8) flags[i];
      }
    }
    break ;

  case NAME_RangeCheckPath:
    checkbbox = oBool(*theo) ;
    break ;

  /* Cannot set RamSize */

  case NAME_MinScreenDetected:
    if ( oType(*theo) == OINTEGER )
      systemparams->MinScreenDetected = (USERVALUE)oInteger(*theo) ;
    else
      systemparams->MinScreenDetected = oReal(*theo) ;
    break ;

  case NAME_RevisionPassword:
    if ( SWpermission(oInteger(*theo), REVISION_PASSWORD_KEY) ) {
      systemparams->RevisionPassword = 1;
      SecSetRevisionPasswordKey(REVISION_PASSWORD_KEY);
    } else if ( SWpermission(oInteger(*theo), REVISION_PASSWORD_KEY2) ) {
      systemparams->RevisionPassword = 1;
      SecSetRevisionPasswordKey(REVISION_PASSWORD_KEY2);
    } else {
      systemparams->RevisionPassword = 0;
      SecSetRevisionPasswordKey(0);
    }
    break ;

  case NAME_DLMS: /* aka IDLOM, HDLT */
    systemparams->DLMS = (int8)SWpermission(oInteger(*theo),
                                           GNKEY_FEATURE_IDLOM);
    break ;

  case NAME_ForceStrokeAdjust:
    if (! set_ForceStrokeAdjust(systemparams, theo))
      return FALSE;
    break ;

  case NAME_ForceRectWidth:
    systemparams->ForceRectWidth = (uint8)oBool(*theo) ;
    break ;

  case NAME_MinLineWidth:
    {
      int32 minlinewidthvals[ 2 ] ;
      if ( oType(*theo) == OINTEGER )
        minlinewidthvals[ 0 ] = minlinewidthvals[ 1 ] = oInteger(*theo) ;
      else {
        OBJECT *olist ;

        olist = oArray(*theo) ;
        switch ( theLen(*theo)) {
        case 2:
          if ( oType( olist[ 0 ] ) != OINTEGER )
            return error_handler( TYPECHECK ) ;
          minlinewidthvals[ 0 ] = oInteger(olist[ 0 ]) ;
          if ( oType( olist[ 1 ] ) != OINTEGER )
            return error_handler( TYPECHECK ) ;
          minlinewidthvals[ 1 ] = oInteger(olist[ 1 ]) ;
          break ;
        case 1:
          if ( oType( olist[ 0 ] ) != OINTEGER )
            return error_handler( TYPECHECK ) ;
          minlinewidthvals[ 0 ] = minlinewidthvals[ 1 ] = oInteger(olist[ 0 ]);
          break ;
        default:
          return error_handler( LIMITCHECK ) ;
        }
      }
      if ( minlinewidthvals[ 0 ] <= 0 ||
           minlinewidthvals[ 1 ] <= 0 )
        return error_handler( RANGECHECK ) ;

      systemparams->MinLineWidth[ 0 ] = minlinewidthvals[ 0 ] ;
      systemparams->MinLineWidth[ 1 ] = minlinewidthvals[ 1 ] ;
    }
    break ;

  case NAME_TrapProLite: /* (extra feature) */
    /* Distinction between TrapProLite and TrapPro has been removed
     * Make the two params synonymous, i.e. is TrapPro available
     */
    systemparams->TrapProLite = (int8)SWpermission(oInteger(*theo),
                                                  GNKEY_FEATURE_TRAP_PRO);
    break ;

  case NAME_PDFOutPassword: /* (extra feature, no UI for now) */
    systemparams->PDFOut = (int8)SWpermission(oInteger(*theo),
                                             GNKEY_FEATURE_PDFOUT);
    break ;

  case NAME_TIFF6: /* 6.0 (extra feature -- ***currently forced enabled***): */
    systemparams->Tiff6 = (int8)TRUE;
                             /* SWpermission(oInteger(*theo),
                                             GNKEY_FEATURE_TIFF6); */
    break ;

  case NAME_TIFFIT: /* (extra feature) */
    systemparams->TiffIT = (int8)SWpermission(oInteger(*theo),
                                             GNKEY_FEATURE_TIFF_IT);
    break ;

  case NAME_HDSLOWRES: /* (extra feature) */
    /* Allow low res HDS. Don't change the params if high res is
     * already on.
     */
    if ( PARAMS_HDS_HIGHRES != systemparams->HDS ) {
      if ( SWpermission(oInteger(*theo), GNKEY_FEATURE_HDSLOWRES) ) {
        systemparams->HDS = (int8)PARAMS_HDS_LOWRES;
      } else {
        systemparams->HDS = (int8)PARAMS_HDS_DISABLED;
      }
    }
    break ;

  case NAME_AccurateRenderThreshold:
  case NAME_AccurateTwoPassThreshold:
    {
      /* integer means use accurate renderer  below that font "size" in pixels.
       * false means never, true means always.
       * null means reset to default of 140 pixels.
       * Integer range is 10..2540, rounded to mul of 10.
       * If it changes, we must purge the font cache.  If chars have
       * already been used in this page, however the cached ones'll be used.
       */
      int32 *threshold = ((name == NAME_AccurateRenderThreshold) ?
                          &systemparams->AccurateRenderThreshold :
                          &systemparams->AccurateTwoPassThreshold);
      int32 old_value = *threshold ;

      if ( oType(*theo) == OINTEGER ) {
        int32 i = oInteger(*theo);
        if ( i < 0 )
          i = 0;
        *threshold = i;
      }
      else if ( oType(*theo) == ONULL )
        /* Set to the default */
        *threshold = ((name == NAME_AccurateRenderThreshold) ?
                      ARENDERTHRESHOLD :
                      ATWOPASSTHRESHOLD);
      else
        *threshold = (oBool(*theo) ? AR_MAXSIZE : 0);

      /* Need to purge the fontcache if the value has changed */
      if ( old_value != *threshold ) {
        fontcache_clear(context->fontsparams);
      }
    }
    break ;

  case NAME_Type1StemSnap:
    {
      /* false means disable, and is the default
         true  means enable bluezone fix but do no adjustment (same as 0)
         integer or float is the offset to subtract before snapping a stem
         to a whole number of pixels.
       */
      USERVALUE param = TYPE1STEMSNAPDISABLED ;

      switch (oType(*theo)) {
      case OBOOLEAN:
        if (oBool(*theo))
          param = 0 ;
        break ;
      case OINTEGER:
        param = (USERVALUE) oInteger(*theo) ;
        break ;
      case OREAL:
        param = oReal(*theo) ;
        break ;
      }

      if (systemparams->Type1StemSnap != param) {
        systemparams->Type1StemSnap = param ;
        fontcache_clear(context->fontsparams) ;
      }
    }
    break ;

  case NAME_DefaultImageFileCache:
    theval = oInteger(*theo) ;
    /* Must be a positive value */
    if (theval < 0)
      return error_handler(RANGECHECK);
    systemparams->DefaultImageFileCache = theval;
    break ;

  case NAME_DetectScreenAngles:
    if ( oType(*theo) == ONULL ) {
      /* NULL object means no detection using angles. */
      systemparams->DoDetectScreenAngles = FALSE ;
      systemparams->DetectScreenAngles[ 0 ] =
       systemparams->DetectScreenAngles[ 1 ] =
        systemparams->DetectScreenAngles[ 2 ] =
         systemparams->DetectScreenAngles[ 3 ] = -1.0f ; /* Never will match. */
    }
    else {
      int32 i, j;
      int32 nulls ;
      SYSTEMVALUE args[ 4 ] ;

      if ( theLen(*theo) != 4 )
        return error_handler( RANGECHECK ) ;

      nulls = 0 ;
      theo = oArray(*theo) ;
      for ( i = 0 ; i < 4 ; ++i ) {
        if ( oType( theo[ i ] ) == ONULL ) {
          /* NULL object means don't know which angle this sep will be screened at. */
          ++nulls ;
          args[ i ] = -1.0 ;
        }
        else {
          SYSTEMVALUE tmpf ;
          if ( ! object_get_numeric(theo + i, & tmpf))
            return FALSE ;

          /* Normalize and snap the angle */
          screen_normalize_angle(&tmpf);

          args[ i ] = tmpf ;
        }
      }

      /*
       * Now we've collected all the angles decide whether to
       * turn on ScreenAngleDetection.
       */
      if (nulls == 4)
        systemparams->DoDetectScreenAngles = FALSE;
      else {
        /* Initialize to on */
        systemparams->DoDetectScreenAngles = TRUE;

        for (i=1; i<4 && systemparams->DoDetectScreenAngles; i++) {
          /* Check that we have got a unique set of angles. */
          for ( j = i - 1 ; j >= 0 ; --j ) {
            if ( args[ j ] == args[i] && args[ j ] >= 0.0 ) {
              /* Non unique values turn it off */
              systemparams->DoDetectScreenAngles = FALSE;
              break;
            }
          }
        }
      }

      systemparams->DetectScreenAngles[ 0 ] = ( USERVALUE )args[ 0 ] ;
      systemparams->DetectScreenAngles[ 1 ] = ( USERVALUE )args[ 1 ] ;
      systemparams->DetectScreenAngles[ 2 ] = ( USERVALUE )args[ 2 ] ;
      systemparams->DetectScreenAngles[ 3 ] = ( USERVALUE )args[ 3 ] ;
    }
    break ;

  case NAME_AdobeSetFlat:
    systemparams->AdobeSetFlat = (uint8)oBool(*theo) ;
    break ;

  case NAME_PoorFlattenpath:
    systemparams->PoorFlattenpath = (uint8)oBool(*theo) ;
    break ;

  case NAME_EnableStroker:
    {
      int32 i, len;
      Bool flags[3];

      len = theLen(*theo) ;
      if (len > 3)
        return error_handler (RANGECHECK);
      theo = oArray(*theo);
      for ( i = 0 ; i < len ; i++, theo++ ) {
        if ( oType(*theo) != OBOOLEAN)
          return error_handler(TYPECHECK);
        flags[i] = oBool(*theo) ;
      }
      for ( i = 0 ; i < len ; i++ ) {
        systemparams->EnableStroker[i] = (uint8)flags[i];
      }
    }
    break ;

  case NAME_AdobeSetLineJoin:
    systemparams->AdobeSetLineJoin = (uint8)oBool(*theo) ;
    break ;

  case NAME_HPSTwo:
    systemparams->HPSTwo = (int8)oBool(*theo) ;
    redoscreens = TRUE ;
    break ;

  case NAME_LowMemImagePurgeToDisk:
  case NAME_PurgeImageSource:
    context->page->imageParams.LowMemImagePurgeToDisk = (uint8)oBool(*theo) ;
    break ;

  case NAME_TickleWarnTime:
    theval = oInteger(*theo) ;
    /* Must be a positive value */
    if (theval < 0)
      return error_handler(RANGECHECK);
    systemparams->TickleWarnTime = (uint32) theval ;
    break ;

  case NAME_PostScriptPassword: /* (extra feature) */
    if (DonglePostScriptDenied()) {
      systemparams->PostScript = (int8)SWpermission(oInteger(*theo),
                                                   GNKEY_FEATURE_POSTSCRIPT);
    }
    break ;

  case NAME_PDFPassword: /* (extra feature) */
    if (DonglePDFDenied()) {
      systemparams->PDF = (int8)SWpermission(oInteger(*theo),
                                            GNKEY_FEATURE_PDF);
    }
    break ;

  case NAME_XPSPassword: /* (extra feature) */
    if (DongleXPSDenied()) {
      systemparams->XPS = (int8)SWpermission(oInteger(*theo),
                                            GNKEY_FEATURE_XPS);
    }
    break ;

  case NAME_ApplyWatermarkPassword: /* (extra feature) */
    if (DongleApplyWatermark()) {
      systemparams->ApplyWatermark = (int8)!SWpermission(oInteger(*theo),
                                                 GNKEY_FEATURE_APPLY_WATERMARK);
    }
    break ;

  case NAME_CompressImageSource:
    context->page->imageParams.CompressImageSource = (int8)oBool(*theo) ;
    break ;

  case NAME_MaxDisplayAndSourceList:
    systemparams->MaxDisplayAndSourceList = oInteger(*theo) ;
    break ;

  case NAME_MaxSourceList:
    systemparams->MaxSourceList = oInteger(*theo) ;
    break ;

  case NAME_MaxInterpreterLevel:
    theval = oInteger(*theo) ;
    if ( theval <= 0 || theval > 256 ) /* Impose an upper limit, for safety */
      return error_handler( RANGECHECK ) ;
    systemparams->MaxInterpreterLevel = theval ;
    break ;


  case NAME_CompressImageParms:
    if ( ! cipUnpack( theo , &context->page->imageParams.CompressImageParms ))
      return FALSE ;
    break ;

  /* Cannot set Picture */

  case NAME_ICC: /* (HIPP) (extra feature) */
    systemparams->ICC = (int8)SWpermission(oInteger(*theo),
                                          GNKEY_FEATURE_ICC);
    break ;

  case NAME_HCMS: /* (extra feature) */
    systemparams->HCMS = (int8)SWpermission(oInteger(*theo),
                                            GNKEY_FEATURE_HCMS);
    break ;

  case NAME_HCMSLite: /* (extra feature) */
    systemparams->HCMSLite = (int8)SWpermission(oInteger(*theo),
                                               GNKEY_FEATURE_HCMS_LITE);
    break ;

  case NAME_HCEP: /* (extra feature) */
    systemparams->HCEP = (int8)SWpermission(oInteger(*theo),
                                           GNKEY_FEATURE_HCEP);
    break ;

  case NAME_DeviceImageOptimization:
    systemparams->DeviceImageOptimization = (uint8)oBool(*theo);
    break ;

  case NAME_TransparencyStrategy:
    if (oInteger(*theo) < 1 || oInteger(*theo) > 2)
      return error_handler(RANGECHECK);
    systemparams->TransparencyStrategy = CAST_SIGNED_TO_UINT8(oInteger(*theo));
    break;

  case NAME_TrapPro: /* (extra feature) */
    systemparams->TrapPro = (int8)SWpermission(oInteger(*theo),
                                              GNKEY_FEATURE_TRAP_PRO);
    break ;

  case NAME_PlatformPassword:
    systemparams->PlatformPassword = (int8)SWpermission(oInteger(*theo),
                                                       PLATFORM_PASSWORD_KEY);
    break ;

  case NAME_EncryptScreens:
    if (oInteger(*theo) < 0 || oInteger(*theo) > 3)
      return error_handler(RANGECHECK);
    systemparams->EncryptScreens = oInteger(*theo);
    break;

  /* Cannot set OSLocale */

  /* Cannot set RIPLocale */

  /* Cannot set OperatingSystem */

  case NAME_StripeImages:
    systemparams->StripeImages = (uint8)oBool(*theo);
    break ;

  case NAME_OptimizeClippedImages:
    systemparams->OptimizeClippedImages = (uint8)oBool(*theo);
    break ;

  case NAME_Rainstorm:
    systemparams->Rainstorm = oInteger(*theo);
    break ;

  case NAME_DLBanding:
    systemparams->DLBanding = oInteger(*theo);
    break ;

  case NAME_NumDisplayLists:
    if ( oInteger(*theo) < 1 )
      return error_handler(RANGECHECK) ;

    /* Silently clip requested pipeline depth to the maximum we can support.
       We always keep a spare slot in the pipeline queue, so there is space
       to effect the render handoff. The pipeline full check happens as we
       leave the renderer spawn, and determines whether interpreting the next
       page would be result in too many DLs in flight. */
    if ( systemparams->Pipelining ) {
      dl_pipeline_depth = oInteger(*theo);
      if ( dl_pipeline_depth >= NUM_DISPLAY_LISTS )
        dl_pipeline_depth = NUM_DISPLAY_LISTS - 1 ;
    } else
      dl_pipeline_depth = 1;

    break ;

  case NAME_Pipelining: /* (extra feature) */
    systemparams->Pipelining = (int8)SWpermission(oInteger(*theo),
                                                  GNKEY_FEATURE_PIPELINING);
    break ;

  case NAME_HVDExternal: /* (extra feature) */
    systemparams->HVDExternal = (int8)SWpermission(oInteger(*theo),
                                                   GNKEY_FEATURE_HVD_EXTERNAL);
    break ;

  case NAME_HVDInternal: /* (extra feature) */
    systemparams->HVDInternal = (int8)SWpermission(oInteger(*theo),
                                                   GNKEY_FEATURE_HVD_INTERNAL);
    break ;

  case NAME_PoorShading:
    systemparams->PoorShading = oBool(*theo);
    break;

  case NAMES_COUNTED: /* Finaliser */
    if ( redoscreens ) {
      if ( ! gsc_redo_setscreen( gstateptr->colorInfo ))
        return FALSE ;
      redoscreens = FALSE ;
    }
    break ;
  }

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            currentsystemparams_ author:              Luke Tunmer
   creation date:       18-Oct-1991       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */

uint8 LicenseID_string[9];

static Bool ps_get_systemparam(corecontext_t *context, uint16 nameid, OBJECT *result)
{
  SYSTEMPARAMS *systemparams = context->systemparams ;
  OBJECT lsnewobj = OBJECT_NOTVM_NOTHING ;

  HQASSERT(result, "No object for userparam result") ;

  theTags( lsnewobj ) = OSTRING | LITERAL | READ_ONLY ;
  SETGLOBJECTTO(lsnewobj, TRUE) ;

  switch (nameid) {

  case NAME_BuildTime:
    object_store_integer(result, systemparams->BuildTime) ;
    break;

  case NAME_BuildDate:
    oString(lsnewobj) = (uint8 *) systemparams->BuildDate ;
    theLen( lsnewobj ) = (uint16) systemparams->BuildDateLen ;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_SourceDate:
    oString(lsnewobj) = (uint8 *) systemparams->SourceDate ;
    theLen( lsnewobj ) = (uint16) systemparams->SourceDateLen ;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_ByteOrder:
    object_store_bool(result, systemparams->ByteOrder) ;
    break;

  case NAME_RealFormat:
    oString( lsnewobj ) = (uint8 *) systemparams->RealFormat ;
    theLen( lsnewobj ) = (uint16) systemparams->RealFormatLen ;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_MaxOutlineCache:
    object_store_integer(result, systemparams->MaxOutlineCache) ;
    break;

  case NAME_CurOutlineCache:
    object_store_integer(result, systemparams->CurOutlineCache) ;
    break;

  case NAME_MaxUPathCache:
    object_store_integer(result, systemparams->MaxUPathCache) ;
    break;

  case NAME_CurUPathCache:
    object_store_integer(result, systemparams->CurUPathCache) ;
    break;

  case NAME_MaxFormCache:
    object_store_integer(result, systemparams->MaxFormCache) ;
    break;

  case NAME_CurFormCache:
    object_store_integer(result, systemparams->CurFormCache) ;
    break;

  case NAME_MaxPatternCache:
    object_store_integer(result, systemparams->MaxPatternCache) ;
    break;

  case NAME_CurPatternCache:
    object_store_integer(result, systemparams->CurPatternCache) ;
    break;

  case NAME_MaxScreenStorage:
    object_store_integer(result, systemparams->MaxScreenStorage) ;
    break;

  case NAME_CurScreenStorage:
    object_store_integer(result, systemparams->CurScreenStorage) ;
    break;

  case NAME_MaxDisplayList:
    object_store_integer(result, systemparams->MaxDisplayList) ;
    break;

  case NAME_CurDisplayList:
    /** \todo ajcd 2011-03-07: Should both DisplayListUsed and CurDisplayList
        reflect inputpage? Perhaps one of them could reflect all pages in
        flight? */
    systemparams->CurDisplayList = CAST_SIZET_TO_INT32(dl_mem_used(context->page));
    object_store_integer(result, systemparams->CurDisplayList) ;
    break;

  case NAME_CurSourceList:
    object_store_integer(result, systemparams->CurSourceList) ;
    break;

  case NAME_LanguageLevel:
    object_store_integer(result, systemparams->LanguageLevel) ;
    break;

  case NAME_ImagemaskBug:
    object_store_bool(result, systemparams->ImagemaskBug) ;
    break;

  case NAME_CompositeFonts:
    object_store_bool(result, systemparams->CompositeFonts) ;
    break;

  case NAME_ColorExtension:
    object_store_bool(result, systemparams->ColorExtension) ;
    break;

  case NAME_ParseComments:
    object_store_bool(result, systemparams->ParseComments) ;
    break;

  case NAME_AutoShowpage:
    object_store_bool(result, systemparams->AutoShowpage) ;
    break;

  case NAME_AutoPrepLoad:
    object_store_bool(result, systemparams->AutoPrepLoad) ;
    break;

  case NAME_PoorPattern:
    object_store_bool(result, systemparams->PoorPattern) ;
    break;

  case NAME_AccurateScreens:
    object_store_bool(result, systemparams->AccurateScreens) ;
    break;

  case NAME_ScreenRotate:
    object_store_bool(result, systemparams->ScreenRotate) ;
    break;

  case NAME_ScreenWithinTolerance:
    object_store_bool(result, systemparams->ScreenWithinTolerance) ;
    break;

  case NAME_ScreenExtraGrays:
    object_store_bool(result, systemparams->ScreenExtraGrays) ;
    break;

  case NAME_ScreenDotCentered:
    object_store_bool(result, systemparams->ScreenDotCentered) ;
    break;

  case NAME_ScreenAngleSnap:
    object_store_bool(result, systemparams->ScreenAngleSnap) ;
    break;

  case NAME_OverrideSpotFunction:
    object_store_integer(result, systemparams->OverrideSpotFunction);
    break;

  case NAME_CompressBands:
    object_store_integer(result, systemparams->CompressBands) ;
    break;

  case NAME_OverrideFrequency:
    object_store_real(result, systemparams->OverrideFrequency) ;
    break;

  case NAME_ScreenZeroAdjust:
    object_store_real(result, systemparams->ScreenZeroAdjust);
    break;

  case NAME_ScreenAngleAccuracy:
    object_store_real(result, systemparams->ScreenAngleAccuracy);
    break;

  case NAME_ScreenFrequencyAccuracy:
    object_store_real(result, systemparams->ScreenFrequencyAccuracy);
    break;

  case NAME_ScreenFrequencyDeviation:
    object_store_real(result, systemparams->ScreenFrequencyDeviation) ;
    break;

  case NAME_MaxGsaves:
    object_store_integer(result, systemparams->MaxGsaves) ;
    break;

  case NAME_CountLines:
    object_store_bool(result, systemparams->CountLines) ;
    break;

  case NAME_PoorStrokepath:
    object_store_bool(result, systemparams->PoorStrokepath) ;
    break;

  case NAME_Level1ExpandDict:
    object_store_bool(result, systemparams->Level1ExpandDict) ;
    break;

  /* note: do not return PreAllocation. It's there only for our use */

  case NAME_ScreenLevels:
    if ( systemparams->GrayLevels == systemparams->ScreenLevels ) {
      object_store_integer(result, systemparams->ScreenLevels) ;
    } else {
      static OBJECT levelsvalues[ SLS_ELEMENTS ] ;

      /* Make sure static object array is marked as a NOTVM array. Since
         it's read-only, it shouldn't be changed, so check_asave shouldn't
         be called on it. */
      object_store_integer(object_slot_notvm(&levelsvalues[0]),
                           systemparams->GrayLevels) ;
      object_store_integer(object_slot_notvm(&levelsvalues[1]),
                           systemparams->ScreenLevels) ;

      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen( *result ) = SLS_ELEMENTS ;
      oArray( *result ) = levelsvalues ;
    }
    break;

  case NAME_DynamicBands:
    object_store_bool(result, systemparams->DynamicBands) ;
    break;

  case NAME_DynamicBandLimit:
    object_store_integer(result, systemparams->DynamicBandLimit) ;
    break;

  case NAME_MaxBandMemory:
    object_store_real(result, systemparams->MaxBandMemory);
    break;

  case NAME_BaseMapSize:
    object_store_integer(result, systemparams->BaseMapSize);
    break;

  case NAME_DisplayListUsed:
    /** \todo ajcd 2011-03-07: Should both DisplayListUsed and CurDisplayList
        reflect inputpage? Perhaps one of them could reflect all pages in
        flight? */
    systemparams->DisplayListUsed = CAST_SIZET_TO_INT32(dl_mem_used(context->page));
    object_store_integer(result, systemparams->DisplayListUsed) ;
    break;

  case NAME_ScreenAngles:
    {
      int32 i ;
      static OBJECT screenvalues[ 4 ] ;
      for ( i = 0 ; i < 4 ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        object_store_real(object_slot_notvm(&screenvalues[i]),
                          systemparams->ScreenAngles[i]) ;
      }

      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen( *result ) = 4 ;
      oArray( *result ) = screenvalues ;
    }
    break;

  case NAME_AsyncMemorySize:
    object_store_integer(result, systemparams->AsyncMemorySize) ;
    break;

  case NAME_MaxScreenTable:
    object_store_integer(result, systemparams->MaxScreenTable * 4) ;
    break;

  case NAME_CacheNewSpotFunctions:
    object_store_bool(result, systemparams->CacheNewSpotFunctions) ;
    break;

  case NAME_ScreenZeroFromRequest:
    object_store_bool(result, systemparams->ScreenZeroFromRequest) ;
    break;

  case NAME_CurInputDevice:
    oString( lsnewobj ) = (uint8 *) systemparams->CurInputDevice ;
    theLen( lsnewobj ) = (uint16) systemparams->CurInputDeviceLen ;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_CurOutputDevice:
    oString( lsnewobj ) = (uint8 *) systemparams->CurOutputDevice ;
    theLen( lsnewobj ) = (uint16) systemparams->CurOutputDeviceLen ;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_OverrideSpotFunctionName:
    oString( lsnewobj ) = systemparams->OverrideSpotFunctionName;
    theLen( lsnewobj ) = systemparams->OverrideSpotFunctionNameLen;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_Revision:
    {
      OBJECT *revision = fast_extract_hash_name(& systemdict, NAME_revision);
      if ( revision == NULL || oType(*revision) != OINTEGER )
        return error_handler(UNREGISTERED);
      OCopy(*result, *revision) ;
    }
    break;

  case NAME_FontResourceDir:
    if (systemparams->FontResourceDirLen < 0) {
      /* It is specified in PSLRM2-Sup-2013 that the default name for
       * the font directory is "fonts/" -- because some downloaders
       * may assume that. Yes, the separator is built into the name.
       */
      HqMemCpy(systemparams->FontResourceDir, "fonts/", 6) ;
      systemparams->FontResourceDirLen = 6; /* strlen_int32("fonts/") */
    }
    oString( lsnewobj ) = systemparams->FontResourceDirLen > 0 ?
      systemparams->FontResourceDir : NULL ;
    theLen( lsnewobj ) = (uint16) systemparams->FontResourceDirLen;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_GenericResourceDir:
    if (systemparams->GenericResourceDirLen < 0) {
      systemparams->GenericResourceDirLen = 0;
    }
    oString( lsnewobj ) = systemparams->GenericResourceDirLen > 0 ?
      systemparams->GenericResourceDir : NULL ;
    theLen( lsnewobj ) = (uint16) systemparams->GenericResourceDirLen;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_GenericResourcePathSep:
    if (systemparams->GenericResourcePathSepLen < 0) {
      systemparams->GenericResourcePathSep[0] = '/' ;
      systemparams->GenericResourcePathSepLen = 1; /* strlen_int32("/") */
    }
    oString( lsnewobj ) = systemparams->GenericResourcePathSepLen > 0 ?
      systemparams->GenericResourcePathSep : NULL ;
    theLen( lsnewobj ) = (uint16) systemparams->GenericResourcePathSepLen;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_JobTimeout:
    object_store_integer(result, jobtimeout) ;
    break;

  case NAME_LicenseID:
    swcopyf(LicenseID_string, (uint8*)"%08d", DongleCustomerNo());
    oString( lsnewobj ) = LicenseID_string;
    theLen( lsnewobj ) = 8;
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_DemoRIP:
    /* v20 isn't customised, so call security compound for info */
    object_store_bool(result, IsWaterMarkRIP()) ;
    break;

  case NAME_WaitTimeout:
    object_store_integer(result, systemparams->WaitTimeout) ;
    break;

  case NAME_OverrideAngle:
    object_store_real(result, systemparams->OverrideAngle) ;
    break;

  case NAME_RelevantRepro:
    object_store_bool(result, systemparams->RelevantRepro) ;
    break;

  case NAME_AdobeSetHalftone:
    object_store_bool(result, systemparams->AdobeSetHalftone) ;
    break;

  case NAME_HalftoneRetention:
    object_store_integer(result, systemparams->HalftoneRetention);
    break;

  case NAME_SeparationMethod:
    {
      uint8 *sepmethodname;
      int32  sepmethodlength;
      get_separation_method(TRUE, &sepmethodname, &sepmethodlength);
      oString( lsnewobj ) = (uint8 *)sepmethodname ;
      theLen( lsnewobj ) = (int16)sepmethodlength ;
      OCopy(*result, lsnewobj) ;
    }
    break;

  case NAME_Separation:
    {
      /* Later on this can go into the rasterStyle or some such. */
      uint16 seplength ;
      uint8 *sepstring ;
      NAMECACHE *sepname = get_separation_name(TRUE);
      HQASSERT( sepname != NULL , "Separation name should not be NULL" ) ;
      if ( sepname == system_names + NAME_Unknown ) {
        /* For backwards compatability. */
        seplength = 0 ;
        sepstring = NULL ;
      } else {
        seplength = sepname->len ;
        sepstring = sepname->clist ;
      }
      oString( lsnewobj ) = sepstring ;
      theLen( lsnewobj ) = seplength ;
      OCopy(*result, lsnewobj) ;
    }
    break;

  case NAME_MaxImagePixel:
    object_store_integer(result, systemparams->MaxImagePixel) ;
    break;

  case NAME_DetectSeparation:
    object_store_bool(result, systemparams->DetectSeparation) ;
    break;

  /* HDS, HMS and HCS not returned deliberately */

  case NAME_MaxResolution:
    object_store_integer(result, DongleMaxResolution()) ;
    break;

  case NAME_ProductName:
    {
      uint8 *product = ProductNameParam() ;

      oString( lsnewobj ) = (uint8 *) product ;
      theLen( lsnewobj ) = (uint16) strlen_int32((char *)product) ;
      OCopy(*result, lsnewobj) ;
    }
    break;

  case NAME_PoorClippath:
    {
      int32 i ;
      static OBJECT clippathflags[ PCP_ELEMENTS ] ;

      for ( i = 0 ; i < PCP_ELEMENTS ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        object_store_bool(object_slot_notvm(&clippathflags[i]),
                          systemparams->PoorClippath[i]) ;
      }
      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen(*result) = PCP_ELEMENTS ;
      oArray(*result) = clippathflags ;
    }
    break;

  case NAME_RangeCheckPath:
    object_store_bool(result, checkbbox) ;
    break;

  case NAME_RamSize: {
    mm_size_t size = mm_total_size();
    object_store_integer(result, size > MAXINT32 ? MAXINT32
                                                 : CAST_SIZET_TO_INT32(size));
    break;
  }
  case NAME_MinScreenDetected:
    object_store_real(result, systemparams->MinScreenDetected) ;
    break;

  /* HPS, HXM not returned deliberately */

  /* RevisionPassword not returned deliberately */

  /* DLMS (aka IDLOM) not returned deliberately */

  case NAME_ForceStrokeAdjust:
    get_ForceStrokeAdjust(systemparams, result) ;
    break;

  case NAME_ForceRectWidth:
    object_store_bool(result, systemparams->ForceRectWidth) ;
    break;

  case NAME_MinLineWidth:
    if ( systemparams->MinLineWidth[ 0 ] == systemparams->MinLineWidth[ 1 ] ) {
      object_store_integer(result, systemparams->MinLineWidth[ 0 ]) ;
    } else {
      static OBJECT minlinewidthvalues[ 2 ] ;

        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
      object_store_integer(object_slot_notvm(&minlinewidthvalues[0]),
                           systemparams->MinLineWidth[0]) ;
      object_store_integer(object_slot_notvm(&minlinewidthvalues[1]),
                           systemparams->MinLineWidth[1]) ;

      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen( *result ) = 2 ;
      oArray( *result ) = minlinewidthvalues ;
    }
    break;

  /* TrapProLite not returned deliberately */

  /* PDF out not returned deliberately */

  /* TIFF 6.0 not returned deliberately */

  /* TIFF-IT not returned deliberately */

  /* HDSLOWRES is part of HDS */

  /* HXMLOWRES is part of HXM */

  case NAME_AccurateRenderThreshold:
    if ( systemparams->AccurateRenderThreshold == 0 ) {
      OCopy(*result, fnewobj) ;
    } else if ( systemparams->AccurateRenderThreshold == AR_MAXSIZE) {
      OCopy(*result, tnewobj) ;
    } else {
      object_store_integer(result, systemparams->AccurateRenderThreshold) ;
    }
    break;

  case NAME_AccurateTwoPassThreshold:
    if ( systemparams->AccurateTwoPassThreshold == 0 ) {
      OCopy(*result, fnewobj) ;
    } else if ( systemparams->AccurateTwoPassThreshold == AR_MAXSIZE ) {
      OCopy(*result, tnewobj) ;
    } else {
      object_store_integer(result, systemparams->AccurateTwoPassThreshold) ;
    }
    break;

  case NAME_Type1StemSnap:
    if (systemparams->Type1StemSnap == TYPE1STEMSNAPDISABLED)
      OCopy(*result, fnewobj) ;
    else
      object_store_real(result, systemparams->Type1StemSnap) ;
    break ;

  case NAME_DefaultImageFileCache:
    object_store_integer(result, systemparams->DefaultImageFileCache) ;
    break;

  case NAME_DetectScreenAngles:
    if ( systemparams->DoDetectScreenAngles ) {
      int32 i ;
      static OBJECT screenvalues[ 4 ] ;
      for ( i = 0 ; i < 4 ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        if ( systemparams->DetectScreenAngles[ i ] >= 0.0f ) {
          object_store_real(object_slot_notvm(&screenvalues[i]),
                            systemparams->DetectScreenAngles[i]) ;
        } else {
          object_store_null(object_slot_notvm(&screenvalues[i])) ;
        }
      }
      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen( *result ) = 4 ;
      oArray( *result ) = screenvalues ;
    } else {
      theTags( *result ) = ONULL ;
    }
    break;

  case NAME_AdobeSetFlat:
    object_store_bool(result, systemparams->AdobeSetFlat) ;
    break;

  case NAME_PoorFlattenpath:
    object_store_bool(result, systemparams->PoorFlattenpath) ;
    break;

  case NAME_EnableStroker:
    {
      int32 i ;
      static OBJECT es_flags[ 3 ] ;

      for ( i = 0 ; i < 3 ; ++i ) {
        /* Make sure static object array is marked as a NOTVM array. Since
           it's read-only, it shouldn't be changed, so check_asave shouldn't
           be called on it. */
        object_store_bool(object_slot_notvm(&es_flags[i]),
                          systemparams->EnableStroker[i]) ;
      }
      theTags( *result ) = OARRAY | LITERAL | READ_ONLY ;
      SETGLOBJECTTO(*result, TRUE) ;
      theLen(*result) = 3;
      oArray(*result) = es_flags ;
    }
    break;

  case NAME_AdobeSetLineJoin:
    object_store_bool(result, systemparams->AdobeSetLineJoin) ;
    break;

  case NAME_HPSTwo:
    object_store_bool(result, systemparams->HPSTwo) ;
    break;

  case NAME_LowMemImagePurgeToDisk:
    object_store_bool(result, context->page->imageParams.LowMemImagePurgeToDisk) ;
    break;

  case NAME_TickleWarnTime:
    object_store_integer(result, systemparams->TickleWarnTime) ;
    break;

  /* PostScriptPassword not returned deliberately */

  /* PDFPassword not returned deliberately */

  /* XPSPassword not returned deliberately */

  /* ApplyWatermarkPassword not returned deliberately */

  case NAME_CompressImageSource:
    object_store_bool(result, context->page->imageParams.CompressImageSource) ;
    break;

  case NAME_MaxDisplayAndSourceList:
    object_store_integer(result, systemparams->MaxDisplayAndSourceList) ;
    break;

  case NAME_MaxSourceList:
    object_store_integer(result, systemparams->MaxSourceList) ;
    break;

  case NAME_MaxInterpreterLevel:
    object_store_integer(result, systemparams->MaxInterpreterLevel) ;
    break;

  /* Watermark not returned deliberately */

  case NAME_CompressImageParms:
    if ( ! cipPack( context->page->imageParams.CompressImageParms , result ))
      return error_handler (UNDEFINEDRESULT);
    break;

  case NAME_Picture:
    object_store_bool(result, systemparams->Picture) ;
    break;

  /* ICC (HIPP) not returned deliberately */

  /* HCMS not returned deliberately */

  /* HCMSLite not returned deliberately */

  /* HCEP not returned deliberately */

  case NAME_DeviceImageOptimization:
    object_store_bool(result, systemparams->DeviceImageOptimization) ;
    break;

  case NAME_TransparencyStrategy:
    object_store_integer(result, systemparams->TransparencyStrategy);
    break;

  /* TrapPro not returned deliberately */

  /* PlatformPassword not returned deliberately */

  case NAME_EncryptScreens:
    object_store_integer(result, systemparams->EncryptScreens);
    break;

  case NAME_OSLocale:
    oString(lsnewobj) = systemparams->OSLocale;
    theLen(lsnewobj) = CAST_TO_UINT16(systemparams->OSLocaleLen);
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_RIPLocale:
    oString(lsnewobj) = systemparams->RIPLocale;
    theLen(lsnewobj) = CAST_TO_UINT16(systemparams->RIPLocaleLen);
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_OperatingSystem:
    oString(lsnewobj) = systemparams->OperatingSystem;
    theLen(lsnewobj) = CAST_TO_UINT16(systemparams->OperatingSystemLen);
    OCopy(*result, lsnewobj) ;
    break;

  case NAME_StripeImages:
    object_store_bool(result, systemparams->StripeImages) ;
    break;

  case NAME_OptimizeClippedImages:
    object_store_bool(result, systemparams->OptimizeClippedImages) ;
    break;

  case NAME_Rainstorm:
    object_store_integer(result, systemparams->Rainstorm);
    break;

  case NAME_DLBanding:
    object_store_integer(result, systemparams->DLBanding);
    break;

  case NAME_NumDisplayLists:
    object_store_integer(result, dl_pipeline_depth);
    break;

  /* Pipelining not returned deliberately */

  /* HVDExternal not returned deliberately */

  /* HVDInternal not returned deliberately */

  /* PoorShading not returned deliberately */

  } /* end of switch */

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* currentsystemparam is an extension which returns a single parameter;
   differs only from currentuserparam_ in that it calls systemparam getters */

Bool currentsystemparam_(ps_context_t *pscontext)
{
  return currentparam(pscontext, pscontext->corecontext->systemparamlist) ;
}

/* ---------------------------------------------------------------------- */
Bool currentsystemparams_(ps_context_t *pscontext)
{
  return currentparams(pscontext, pscontext->corecontext->systemparamlist) ;
}

static module_params_t ps_system_params = {
  system_match,
  ps_set_systemparam,
  ps_get_systemparam,
  NULL
} ;

/* initSystemParams - initialization for the system parameters */
static Bool systemparams_swinit(struct SWSTART *params)
{
  corecontext_t *context = get_core_context() ;
  SYSTEMPARAMS *systemparams ;

  UNUSED_PARAM(struct SWSTART *, params) ;

  HQASSERT(ps_system_params.next == NULL,
           "Already linked system params accessor") ;

  /* Link accessors into global list */
  ps_system_params.next = context->systemparamlist ;
  context->systemparamlist = &ps_system_params ;

  /* Set context systemparams pointer */
  systemparams = context->systemparams = &system_params ;

  systemparams->BuildTime = build_time ;
  systemparams->BuildDate = build_date ;
  systemparams->BuildDateLen = strlen_int32(( char * )build_date ) ;
  systemparams->SourceDate = (uint8 *)QUOTED_SOURCE_DATE ;
  systemparams->SourceDateLen = strlen_int32((char *)systemparams->SourceDate) ;
  systemparams->PostScript = (int8) ! DonglePostScriptDenied();
  systemparams->PDF = (int8) ! DonglePDFDenied();
  systemparams->HPS = (int8) ! DongleHPSDenied();
  systemparams->XPS = (int8) ! DongleXPSDenied();
  systemparams->ApplyWatermark = (int8) DongleApplyWatermark();

  systemparams->OSLocale = get_oslocale();
  systemparams->OSLocaleLen = strlen_int32((char*)systemparams->OSLocale);
  systemparams->RIPLocale = get_guilocale();
  systemparams->RIPLocaleLen = strlen_int32((char*)systemparams->RIPLocale);

  systemparams->OperatingSystem = get_operatingsystem();
  systemparams->OperatingSystemLen = strlen_int32((char*)systemparams->OperatingSystem);


  return TRUE ;
}


/* finishSystemParams - deinitialization for the system parameters */
static void systemparams_finish(void)
{
}

void systemparams_C_globals(core_init_fns *fns)
{
  SYSTEMPARAMS system_params_init = {
    SysPasswdString ,  /*   SystemParamsPassword */
    0 ,                /*   SystemParamsPasswordLen */
    JobPasswdString ,  /*   StartJobPassword */
    0 ,                /*   StartJobPasswordLen */
    0 ,                /*   BuildTime */
    NULL ,             /*   BuildDate */
    0 ,                /*   BuildDateLen */
#ifdef highbytefirst
    0 ,                /*   ByteOrder */
#else
    1 ,                /*   ByteOrder */
#endif
    (uint8*)"IEEE" ,   /*   RealFormat */
    4 ,                /*   RealFormatLen */
    0 ,                /*   MaxOutlineCache */
    0 ,                /*   CurOutLineCache */
    0 ,                /*   MaxUPathCache (overridden by setDynamicGlobalDefaults()) */
    0 ,                /*   CurUPathCache */
    0 ,                /*   MaxFormCache */
    0 ,                /*   CurFormCache */
    0 ,                /*   MaxPatternCache */
    0 ,                /*   CurPatternCache */
    0 ,                /*   MaxScreenStorage */
    0 ,                /*   CurScreenStorage */
    MAXINT32,          /*   MaxDisplayList */
    0 ,                /*   CurDisplayList */
    MAXINT32,          /*   MaxDisplayAndSourceList */
    0 ,                /*   MaxSourceList */
    0 ,                /*   CurSourceList */
    3 ,                /*   LanguageLevel */
    0 ,                /*   ImageMaskBug */
    1 ,                /*   CompositeFonts */
    1 ,                /*   ColorExtension */
    0 ,                /*   ParseComments */
    0 ,                /*   AutoShowpage */
    1 ,                /*   AutoPrepLoad */
    0 ,                /*   PoorPattern */
    0 ,                /*   AccurateScreens */
    0 ,                /*   ScreenRotate */
    0 ,                /*   ScreenWithinTolerance */
    0 ,                /*   ScreenExtraGrays */
    0 ,                /*   ScreenDotCentered */
    1 ,                /*   ScreenAngleSnap */
    -1 ,               /*   OverrideSpotFunction */
    7 ,                /*   CompressBands */
    0.0f ,             /*   OverrideFrequency */
    0.07f ,            /*   ScreenZeroAdjust */
    0.004f ,           /*   ScreenAngleAccuracy */
    0.020f ,           /*   ScreenFrequencyAccuracy */
    0.07f ,            /*   ScreenFrequencyDeviation */
    0 ,                /*   MaxGsaves */
    42 ,               /*   MaxInterpreterLevel */
    FALSE ,            /*   CountLines */
    FALSE ,            /*   PoorStrokepath */
    TRUE ,             /*   Level1ExpandDict */
    TRUE ,             /*   CacheNewSpotFunctions */
    1.0f ,             /*   PreAllocation */
    1024 ,             /*   GrayLevels */
    TRUE ,             /*   DynamicBands */
    0 ,                /*   DynamicBandLimit (overridden by setDynamicGlobalDefaults()) */
    0.0f,              /*   MaxBandMemory (overridden by setDynamicGlobalDefaults()) */
    BASEMAP_DEFAULT_SIZE, /* BaseMapSize */
    0,                 /*   DisplayListUsed */
    { 15.0f , 75.0f , 0.0f , 45.0f },
    /*   ScreenAngles - C M Y K angles (all 0.0 implies don't know) */
    25000,             /*   AsyncMemorySize */
    262144,            /*   MaxScreenTable (1Mb default, expressed in
                            2x16bit words) */
    FALSE,             /*   ScreenZeroFromRequest */
    TRUE,              /*   RelevantRepro */
#if 0
    0, 0,              /*   padding */
#endif
    CurInputDeviceString,         /* CurInputDevice */
    0,
    CurOutputDeviceString,        /* CurOutputDevice */
    0,
    OverrideSFString, /*   OverrideSpotFunction */
    0,
    FontResourceDirString,            /* FontResourceDir */
    -1,                               /* FontResourceDirLen: -1 ==> needs initialization */
    GenericResourceDirString,         /* GenericResourceDir */
    -1,                               /* GenericResourceDirLen: -1 ==> needs initialization  */
    GenericResourcePathSepString,     /* GenericResourcePathSep */
    -1,                                   /* GenericResourcePathSepLen: -1 ==> needs initialization */
    300,                                /* WaitTimeout */
    -1.0f ,           /* OverrideAngle */
    1024 ,            /* ScreenLevels */
    FALSE,            /* AdobeSetHalftone */
    3,                /* HalftoneRetention (XPS increments by 3/page) */
    8192,             /* MaxImagePixel */
    TRUE,             /* DetectSeparation */
    PARAMS_HDS_DISABLED, /* HDS  - not available */
    0,                /* HMS  - not available */
    0,                /* HCS  - not available */
    1,                /* HPS  -     available */
    PARAMS_HXM_DISABLED, /* HXM  - not available */
    { FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE },
    /*   PoorClippath */
    NULL ,            /* SourceDate */
    0 ,               /* SourceDateLen */
    20.0f,            /* MinScreenDetected */
    0,                /* RevisionPassword */
#ifdef WOODPECKER
    /*
     * Yuck!
     * DSJ take Woodpecker and expect HDLT to be on
     * all the time as thats the way it was in the
     * old branch. We need to sort out their customer
     * number etc and have a password for them. This
     * works for now.
     */
    1,                /* DLMS */
#else
    0,                /* DLMS */
#endif
    COMPATIBILITY_TRUE, /*   ForceStrokeAdjust */
    FALSE,             /*   ForceRectWidth */
    0,                 /*   spare */
    { 1, 1 },          /*   MinLineWidth */
    0 ,                /*   TrapProLite - not available */
    0 ,                /*   TrapPro - not available */
    0,                 /*   TIFF-IT - not available */
#define ARENDERTHRESHOLD (140) /* AccurateRenderThreshold default pixel size */
    ARENDERTHRESHOLD,     /* AccurateRenderThreshold */
#define ATWOPASSTHRESHOLD (20) /* AccurateTwoPassThreshold default pixel size */
    ATWOPASSTHRESHOLD, /*   AccurateTwoPassThreshold */
    TYPE1STEMSNAPDISABLED, /* Type1StemSnap adjustment and correct BlueZone handling */
    TRUE,              /*   PS enabled */
    TRUE,              /*   PDF enabled */
    TRUE,              /*   XPS enabled */
    FALSE,             /*   Apply watermark */
    32768,             /*   Default size of image file cache buffer, 32 * 1024 bytes */
    TRUE,              /*   TIFF 6.0 - available by current default */
    0,                 /*   ICC (HIPP) - not available */
    0,                 /*   HCMS - not available */
    0,                 /*   HCMSLite - not available */
    0,                 /*   HCEP - not available */
    FALSE,
    { -1.0f , -1.0f , -1.0f , -1.0f },
    /*   DetectScreenAngles - C M Y K angles used for color detection. */
    TRUE,              /*   AdobeSetFlat */
    FALSE,             /*   PoorFlattenpath */
    { TRUE , FALSE, TRUE }, /* EnableStroker */
    TRUE,              /*   AdobeSetLineJoin */
    TRUE,              /*   HPS2: Default is ON for this release */
    0,                 /*   TickleWarnTime */
    FALSE,             /*   Picture */
    TRUE,              /*   DeviceImageOptimization */
    { { { ONULL | LITERAL, ISGLOBAL, 0 } }, { { NULL } } }, /* Watermark */
    2,                /* TransparencyStrategy */
    0,                /* PlatformPassword */
    0,                /* EncryptScreens */
    NULL,             /* OSLocale */
    0,                /* OSLocaleLen */
    NULL,             /* RIPLocale */
    0,                /* RIPLocaleLen */
    NULL,             /* OperatingSystem */
    0,                /* OperatingSystemLen */
    TRUE,             /* StripeImages */
    TRUE,             /* OptimizeClippedImages */
    0,                /* Rainstorm */
    0,                /* DLBanding */
    0,                /* Pipelining */
    0,                /* HVDExternal */
    0,                /* HVDInternal */
    FALSE             /* PoorShading */
  } ;

  system_params = system_params_init ;
  ps_system_params.next = NULL ;
  fns->swinit = systemparams_swinit ;
  fns->finish = systemparams_finish ;
}

/* Log stripped */
