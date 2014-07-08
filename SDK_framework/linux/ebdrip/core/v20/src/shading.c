/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!src:shading.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Operators and parameters for shaded fills.
 */

#include "core.h"
#include "swerrors.h"
#include "swoften.h"
#include "objects.h"
#include "fileio.h"
#include "swpdfout.h"
#include "metrics.h"
#include "namedef_.h"
#include "often.h"
#include "routedev.h"
#include "vndetect.h"
#include "idlom.h"
#include "plotops.h"
#include "dl_bres.h"
#include "utils.h"
#include "gu_rect.h"
#include "swmemory.h"
#include "genhook.h"
#include "gu_path.h"  /* fill_four(), i4cpath */
#include "spdetect.h"
#include "halftone.h" /* ht_defer_allocation */
#include "color.h" /* ht_getClear */
#include "gu_ctm.h"
#include "ripdebug.h"
#include "dl_free.h"  /* free_dl_object */
#include "tables.h"
#include "blends.h"
#include "gouraud.h"
#include "tensor.h"
#include "shading.h"
#include "params.h"
#include "shadecev.h" /* mm_pool_shading */
#include "shadesetup.h" /* vertex_*, mm_pool_shading */
#include "gschtone.h"
#include "rcbcntrl.h" /* rcbn_current_colorant(), ... */
#include "rcbshfil.h" /* rcbs_* */

#if defined( DEBUG_BUILD )
/* Debug value, used to choose DL rendering vs front end fills (bit 0), and
   outline vs. filled decomposition (bit 1) */
int32 shading_debug_flag = 0; /* SHADING_DEBUG_OUTLINE ; */

void init_shading_debug(void)
{
  register_ripvar(NAME_debug_sh, OINTEGER, &shading_debug_flag) ;
}
#endif

#if defined(METRICS_BUILD) || defined(DEBUG_BUILD)
int32 n_gouraud_triangles = 0 ;
#endif

#ifdef METRICS_BUILD
int32 n_coons_patches = 0 ;
int32 n_tensor_patches = 0 ;
int32 n_decomposed_triangles = 0 ;
int32 max_decomposition_depth = 0 ;
int32 max_decomposition_triangles = 0 ;
int32 max_gouraud_dl_size = 0 ;
int32 total_gouraud_dl_size = 0 ;
/* Temporaries for instrumentation */
int32 this_decomposed_triangles = 0 ;
int32 this_decomposition_depth = 0 ;

static Bool shading_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("DL")) ||
       !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Shading")) )
    return FALSE ;
  SW_METRIC_INTEGER("n_gouraud_triangles", n_gouraud_triangles) ;
  SW_METRIC_INTEGER("n_coons_patches", n_coons_patches) ;
  SW_METRIC_INTEGER("n_tensor_patches", n_tensor_patches) ;
  SW_METRIC_INTEGER("n_decomposed_triangles", n_decomposed_triangles) ;
  SW_METRIC_INTEGER("max_decomposition_depth", max_decomposition_depth) ;
  SW_METRIC_INTEGER("max_decomposition_triangles", max_decomposition_triangles) ;
  SW_METRIC_INTEGER("max_gouraud_dl_size", max_gouraud_dl_size) ;
  SW_METRIC_INTEGER("total_gouraud_dl_size", total_gouraud_dl_size) ;
  sw_metrics_close_group(&metrics) ; /* Shading */
  sw_metrics_close_group(&metrics) ; /* DL */

  return TRUE ;
}

static void shading_metrics_reset(int reason)
{
  UNUSED_PARAM(int, reason) ;

  n_gouraud_triangles = 0 ;
  n_coons_patches = 0 ;
  n_tensor_patches = 0 ;
  n_decomposed_triangles = 0 ;
  max_decomposition_depth = 0 ;
  max_decomposition_triangles = 0 ;
  max_gouraud_dl_size = 0 ;
  total_gouraud_dl_size = 0 ;
  /* Temporaries for instrumentation */
  this_decomposed_triangles = 0 ;
  this_decomposition_depth = 0 ;
}

static sw_metrics_callbacks shading_metrics_hook = {
  shading_metrics_update,
  shading_metrics_reset,
  NULL
} ;
#endif /* METRICS_BUILD */

static SHADINGinfo *alloc_SHADINGinfo(DL_STATE *page) ;

static int32 base_fuid = FN_GEN_DEFAULT ;

/* Static object array for default decode/domain values. (This is awful code! --johnk) */
static OBJECT default_decode_olist[4] = {
  OBJECT_NOTVM_INTEGER(0), OBJECT_NOTVM_INTEGER(1),
  OBJECT_NOTVM_INTEGER(0), OBJECT_NOTVM_INTEGER(1),
} ;

static OBJECT default_decode =
  OBJECT_NOTVM_ARRAY(default_decode_olist, 2) ;

/* Dictionary matches for parsing smooth shading. is_shadingdict walks these,
   the rest of the shading code asserts that things are OK as expected. */
enum {
  shadematch_ShadingType, shadematch_ColorSpace, shadematch_BBox,
  shadematch_Background, shadematch_AntiAlias,
  shadematch_n_entries
} ;
static NAMETYPEMATCH shadematch[shadematch_n_entries + 1] = { /* Top level shading dict */
  { NAME_ShadingType, 1, { OINTEGER } },
  { NAME_ColorSpace, 3, { ONAME, OARRAY, OPACKEDARRAY } },
  { NAME_BBox | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_Background | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_AntiAlias | OOPTIONAL, 1, { OBOOLEAN } },
  DUMMY_END_MATCH
} ;

enum {
  shadematch1_Domain, shadematch1_Matrix, shadematch1_Function,
  shadematch1_n_entries
} ;
static NAMETYPEMATCH shadematch1[shadematch1_n_entries + 1] = { /* Type 1 Shading */
  { NAME_Domain | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_Matrix | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_Function, 4, { OARRAY, OPACKEDARRAY, ODICTIONARY, OFILE } },
  DUMMY_END_MATCH
} ;

enum {
  shadematch23_Coords, shadematch23_Domain, shadematch23_Function,
  shadematch23_Extend, shadematch23_HqnOpacity,
  shadematch23_n_entries
} ;
static NAMETYPEMATCH shadematch23[shadematch23_n_entries + 1] = { /* Types 2 and 3 */
  { NAME_Coords, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_Domain | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_Function, 4, { OARRAY, OPACKEDARRAY, ODICTIONARY, OFILE } },
  { NAME_Extend | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY } },
  { NAME_HqnOpacity | OOPTIONAL, 4, { OARRAY, OPACKEDARRAY, ODICTIONARY, OFILE } },
  DUMMY_END_MATCH
} ;

enum {
  shadematch4567_VerticesPerRow, shadematch4567_DataSource,
  shadematch4567_Function,
  shadematch4567_n_entries
} ;
static NAMETYPEMATCH shadematch4567[shadematch4567_n_entries + 1] = { /* Types 4, 5, 6, 7 */
  { NAME_VerticesPerRow, 1, { OINTEGER } }, /* Type 5 only */
  { NAME_DataSource, 4, { OSTRING, OFILE, OARRAY, OPACKEDARRAY } },
  { NAME_Function | OOPTIONAL, 4, { OARRAY, OPACKEDARRAY, ODICTIONARY, OFILE } },
  DUMMY_END_MATCH
} ;

enum {
  datasource4567_BitsPerFlag, datasource4567_BitsPerCoordinate,
  datasource4567_BitsPerComponent, datasource4567_Decode,
  datasource4567_n_entries
} ;
static NAMETYPEMATCH datasource4567[datasource4567_n_entries + 1] = { /* Types 4, 5, 6, 7 DataSource */
  { NAME_BitsPerFlag, 1, { OINTEGER } }, /* Not Type 5 */
  { NAME_BitsPerCoordinate, 1, { OINTEGER } },
  { NAME_BitsPerComponent, 1, { OINTEGER } },
  { NAME_Decode, 2, { OARRAY, OPACKEDARRAY } },
  DUMMY_END_MATCH
} ;

void init_C_globals_shading(void)
{
#if defined( DEBUG_BUILD )
  shading_debug_flag = 0;
  n_gouraud_triangles = 0 ; /* In case DEBUG but not METRICS */
#endif
#ifdef METRICS_BUILD
  shading_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&shading_metrics_hook) ;
#endif
  base_fuid = FN_GEN_DEFAULT ;
}

/** setsmoothness and currentsmoothness set and return the smoothness parameter
   that defines the maximum colour error allowed by shaded fills. */
Bool gs_setsmooth(STACK *stack)
{
  USERVALUE arg ;

  HQASSERT( stack , "stack is null in gs_setsmooth" ) ;

  if ( isEmpty(*stack) )
    return error_handler(STACKUNDERFLOW) ;

  if ( !object_get_real(theTop(*stack), &arg) )
    return FALSE ;

  if ( arg < 0.0f )
    arg = 0.0f ;
  else if ( arg > 1.0f )
    arg = 1.0f ;

  thegsSmoothness(*gstateptr) = arg ;

  pop(stack) ;

  return TRUE ;
}

USERVALUE gs_currsmooth(corecontext_t *context, GSTATE *gs)
{
  USERPARAMS *userparams = context->userparams;
  USERVALUE result = thegsSmoothness(*gs) ;

  /* Range limit smoothness parameter using userparam limits. */
  HQASSERT(userparams->MinSmoothness >= 0.0f,
           "MinSmoothness out of range") ;
  HQASSERT(userparams->MaxSmoothness <= 1.0f,
           "MaxSmoothness out of range") ;
  HQASSERT(userparams->MinSmoothness <= userparams->MaxSmoothness,
           "MinSmoothness greater than MaxSmoothness") ;
  if ( result < userparams->MinSmoothness )
    result = userparams->MinSmoothness ;
  else if ( result > userparams->MaxSmoothness )
    result = userparams->MaxSmoothness ;

  return result ;
}

/** Operator to get current smoothness. */
Bool currsmooth_(ps_context_t *pscontext)
{
  return stack_push_real(gs_currsmooth(ps_core_context(pscontext), gstateptr),
                         &operandstack) ;
}

/* Operator to set current smoothness. */
Bool setsmooth_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return gs_setsmooth(&operandstack) ;
}

/* DataSource extraction routines. */

/** more_mesh_data tests if there is more data remaining. */
Bool more_mesh_data(SHADINGsource *src)
{
  FILELIST *flptr ;
  int32 c ;

  HQASSERT(src, "data parameter NULL in more_mesh_data") ;

  switch ( oType(src->odata) ) {
  case OARRAY:
  case OPACKEDARRAY:
  case OSTRING:
    if ( theLen(src->odata) == 0 )
      return FALSE ;
    break ;
  case OFILE:
    flptr = oFile(src->odata) ;
    if ( (c = Getc(flptr)) == EOF )
      return FALSE ;
    UnGetc(c, flptr) ;
    break ;
  default:
    HQFAIL("Bad data source object in more_mesh_data") ;
    return error_handler(UNREGISTERED) ;
  }

  HQASSERT(src->nbitsleft < 8, "more than 8 bits left in more_mesh_data") ;
  src->nbitsleft = 0 ; /* Forget about remainder */

  return TRUE ;
}

/** get_mesh_value extracts a number of bits, and either assigns to an integer
   or decodes the bits to a real in a supplied range. */
Bool get_mesh_value(SHADINGsource *src, int32 bits, SYSTEMVALUE *decode,
                     SYSTEMVALUE *svalue, int32 *ivalue)
{
 /* Maximum value representable in bits. Shift by 32 is undefined. */
  uint32 max = (uint32)(bits == 32 ? -1 : (-1 << bits) ^ -1) ;
  uint32 ext ; /* current value */

  HQASSERT(src, "data parameter NULL in get_mesh_value") ;
  HQASSERT(svalue || ivalue,
           "svalue/ivalue parameters both NULL in get_mesh_value") ;
  HQASSERT(!(svalue && ivalue),
           "Only one of svalue/ivalue parameters allowed in get_mesh_value") ;
  HQASSERT(bits >= 0 && bits <= 32, "bits parameter out of range in get_mesh_value") ;

  switch ( oType(src->odata) ) {
  case OARRAY:
  case OPACKEDARRAY:
    if ( theLen(src->odata)-- > 0 ) {
      OBJECT *theo = oArray(src->odata)++ ;

      switch ( oType(*theo) ) {
      case OINTEGER:
        if ( ivalue )
          *ivalue = oInteger(*theo) ;
        else
          *svalue = (SYSTEMVALUE)oInteger(*theo) ;
        return TRUE ;
      case OREAL:
        if ( svalue ) {
          *svalue = oReal(*theo) ;
          return TRUE ;
        } /* else FALLTHROUGH, since real not applicable to integer-only slot */
      default:
        return error_handler(TYPECHECK) ;
      }
    } else
      return error_handler(RANGECHECK) ; /* No more data, but incomplete */
    /* NOTREACHED */
  case OSTRING:
    while ( src->nbitsleft < bits ) { /* Get more bits until enough */
      uint8 *chptr = oString(src->odata)++ ;

      if ( theLen(src->odata)-- == 0 ) /* No more data, but incomplete */
        return error_handler(RANGECHECK) ;

      src->value <<= 8 ;
      src->value |= *chptr ;
      src->nbitsleft += 8 ;
    }
    break ;
  case OFILE:
    while ( src->nbitsleft < bits ) { /* Get more bits until enough */
      FILELIST *flptr = oFile(src->odata) ;
      int32 byte = Getc(flptr) ;

      if ( byte == EOF ) /* No more data, but incomplete */
        return error_handler(RANGECHECK) ;

      src->value <<= 8 ;
      src->value |= byte ;
      src->nbitsleft += 8 ;
    }
    break ;
  default:
    HQFAIL("Bad data source type in get_mesh_value") ;
    return error_handler(UNREGISTERED) ;
  }

  HQASSERT(src->nbitsleft >= bits, "Bitsleft too small in get_mesh_value") ;

  /* Extract top "bits" number of bits from data value */
  src->nbitsleft -= bits ;
  ext = (src->value >> src->nbitsleft) & max ;

  if ( ivalue ) {
    *ivalue = (int32)ext ;
  } else {
    HQASSERT(decode, "Decode parameter NULL in get_mesh_value") ;
    *svalue = decode[0] + (decode[1] - decode[0]) * (SYSTEMVALUE)ext / (SYSTEMVALUE)max ;
  }

  return TRUE ;
}


/** Extract a vertex flag from the shading data stream */
Bool get_vertex_flag(SHADINGsource *src, int32 *flagptr)
{
  HQASSERT(src, "src parameter NULL in get_vertex_flag") ;
  HQASSERT(flagptr, "flagptr parameter NULL in get_vertex_flag") ;

  return get_mesh_value(src, src->bitsperflag, NULL, NULL, flagptr) ;
}

/** Extract a pair of coordinates from the shading data stream */
Bool get_vertex_coords(SHADINGsource *src, SYSTEMVALUE *px, SYSTEMVALUE *py)
{
  SYSTEMVALUE x, y, *decode ;

  HQASSERT(src, "src parameter NULL in get_vertex_coords") ;
  HQASSERT(px, "px parameter NULL in get_vertex_coords") ;
  HQASSERT(py, "py parameter NULL in get_vertex_coords") ;

  decode = src->decode ;
  if ( !get_mesh_value(src, src->bitspercoord, decode, &x, NULL) )
    return FALSE ;

  if ( decode )
    decode += 2 ;

  if ( !get_mesh_value(src, src->bitspercoord, decode, &y, NULL) )
    return FALSE ;

  /* Convert vertex points to device space */
  MATRIX_TRANSFORM_XY(x, y, *px, *py, &thegsPageCTM(*gstateptr)) ;
  return TRUE ;
}

/** Extract a color from the shading data stream. */
Bool get_vertex_color(SHADINGsource *src, SHADINGvertex *vtx)
{
  int32 index ;
  SYSTEMVALUE *decode ;

  HQASSERT(src, "src parameter NULL in get_vertex_color") ;
  HQASSERT(vtx, "vertex parameter NULL in get_vertex_color") ;

  decode = src->decode ;

  if ( decode )
    decode += 4 ;

  /* Unpack colour values. If icolinfo is not NULL, then this is an indexed
     colorspace, and should be converted immediately. */
  if ( isIShadingInfoIndexed(src->sinfo) ) { /* Indexed space */
    SYSTEMVALUE temp ;
    USERVALUE utemp , *uresults ;
    int32 dimension ;

    if ( !get_mesh_value(src, src->bitspercomp, decode, &temp, NULL) )
      return FALSE ;

    utemp = (USERVALUE)temp ;

    if ( !gsc_setcolordirect(gstateptr->colorInfo, GSC_SHFILL, &utemp) )
      return FALSE ;

    /* This will invoke the chain and fetch the colour indexed */
    if ( !gsc_baseColor( gstateptr->colorInfo, GSC_SHFILL, &uresults, &dimension ) )
      return FALSE ;
    HQASSERT( dimension == src->sinfo->ncomps ,
      "gsc_baseColor returned unexpected number of dimensions" ) ;

    for ( index = 0 ; index < src->sinfo->ncomps ; ++index ) {
      vtx->comps[index] = uresults[index] ;
    }
  } else {
    for ( index = 0 ; index < src->sinfo->ncomps ; ++index ) {
      SYSTEMVALUE temp ;

      if ( !get_mesh_value(src, src->bitspercomp, decode, &temp, NULL) )
        return FALSE ;

      vtx->comps[index] = (USERVALUE)temp ;
      if ( decode )
        decode += 2 ;
    }
  }

  if ( vtx->converted )
    dlc_release(src->sinfo->page->dlc_context, &vtx->dlc) ;
  if ( vtx->probeconverted )
    dlc_release(src->sinfo->page->dlc_context, &vtx->dlc_probe) ;

  vtx->opacity = 1.0 ; /* opaque */
  vtx->converted = FALSE ;
  vtx->probeconverted = FALSE ;
  vtx->upwards = TRUE ;

  return TRUE ;
}

/** Unpack vertex information from freeform SHADINGsource into vertex structure */
static Bool get_vertex_freeform(SHADINGsource *src, SHADINGvertex *vtx, int32 *flagptr)
{

  HQASSERT(src, "src parameter NULL in get_vertex_freeform") ;
  HQASSERT(vtx, "vertex parameter NULL in get_vertex_freeform") ;
  HQASSERT(flagptr, "flagptr parameter NULL in get_vertex_freeform") ;

  if ( !get_vertex_flag( src, flagptr ) )
    return FALSE ;

  if ( *flagptr < 0 || *flagptr > 2 )
    return error_handler(RANGECHECK) ;

  /* X and Y coordinates */
  if ( !get_vertex_coords( src, &vtx->x, &vtx->y ) )
    return FALSE ;

  if ( !get_vertex_color( src, vtx ) )
    return FALSE ;

  return TRUE ;
}

/** Unpack vertex information from lattice SHADINGsource into vertex structure */
static Bool get_vertex_lattice(SHADINGsource *src, SHADINGvertex *vtx)
{

  HQASSERT(src, "src parameter NULL in get_vertex_lattice") ;
  HQASSERT(vtx, "vertex parameter NULL in get_vertex_lattice") ;

  /* X and Y coordinates */
  if ( !get_vertex_coords( src, &vtx->x, &vtx->y ) )
    return FALSE ;

  if ( !get_vertex_color( src, vtx ) )
    return FALSE ;

  return TRUE ;
}

static Bool datasource_array(SHADINGinfo *sinfo, SHADINGsource *source, OBJECT *theo)
{
  HQASSERT(theo, "theo parameter NULL in datasource_array") ;
  HQASSERT(source, "source parameter NULL in datasource_array") ;

  OCopy(source->odata, *theo) ;
  source->bitspercomp = 0 ; /* Meaningless for array */
  source->bitspercoord = 0 ; /* Meaningless for array */
  source->bitsperflag = 0 ; /* Meaningless for array */
  source->nbitsleft = 0 ; /* Meaningless for array */
  source->value = 0 ; /* Meaningless for array */
  source->decode = NULL ; /* Meaningless for array */
  source->sinfo = sinfo ;

  return TRUE ;
}

static Bool datasource_string(SHADINGinfo *sinfo, SHADINGsource *source, OBJECT *theo,
                               int32 bitspercomp, int32 bitspercoord,
                               int32 bitsperflag, SYSTEMVALUE *decode)
{
  HQASSERT(theo, "theo parameter NULL in datasource_string") ;
  HQASSERT(source, "source parameter NULL in datasource_string") ;

  OCopy(source->odata, *theo) ;
  source->bitspercomp = bitspercomp ;
  source->bitspercoord = bitspercoord ;
  source->bitsperflag = bitsperflag ;
  source->nbitsleft = 0 ;
  source->value = 0 ;
  source->decode = decode ;
  source->sinfo = sinfo ;
  return TRUE ;
}

static Bool datasource_stream(SHADINGinfo *sinfo, SHADINGsource *source, OBJECT *theo,
                               int32 bitspercomp, int32 bitspercoord,
                               int32 bitsperflag,
                               SYSTEMVALUE *decode)
{
  FILELIST *flptr ;

  HQASSERT(theo, "theo parameter NULL in datasource_stream") ;
  HQASSERT(source, "source parameter NULL in datasource_stream") ;

  OCopy(source->odata, *theo) ;
  source->bitspercomp = bitspercomp ;
  source->bitspercoord = bitspercoord ;
  source->bitsperflag = bitsperflag ;
  source->nbitsleft = 0 ;
  source->value = 0 ;
  source->decode = decode ;
  source->sinfo = sinfo ;


  flptr = oFile(*theo) ;
  HQASSERT(flptr, "flptr is null in datasource_stream") ;

  if ( !isIInputFile(flptr) || !isIOpenFileFilter(theo, flptr) )
    return error_handler(IOERROR) ;

  /* Reset RSD filter */
  if ( isIEof(flptr) && isIRewindable(flptr) ) {
    if ( (*theIMyResetFile(flptr))(flptr) == EOF )
      return error_handler(IOERROR) ;
  }

  return TRUE ;
}

/** Test if object is an N:M function dictionary */
Bool is_functiondict(OBJECT *theo, SHADINGinfo *sinfo, int32 offset)
{
  FUNCTIONINFO info ;

  if ( !fn_get_info(theo, FN_SHADING, offset, sinfo->base_fuid,
                    FN_GEN_NA, &sinfo->fndecode, &info) )
    return FALSE ;

  if ( info.in_dim != sinfo->ncomps )
    return error_handler(TYPECHECK) ;

  if ( info.out_dim != sinfo->noutputs )
    return error_handler(RANGECHECK) ;

  return TRUE ;
}

/** Test if object is a shading dictionary. Walks dictionary matches as
   appropriate. */
Bool is_shadingdict(OBJECT *thed)
{
  OBJECT *theo ;
  int32 type, i ;

  HQASSERT(thed, "Null object in is_shadingdict") ;

  if ( oType(*thed) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  if ( !oCanRead(*oDict(*thed)) && !object_access_override(oDict(*thed)) )
    return error_handler(INVALIDACCESS) ;

  if ( !dictmatch(thed, shadematch) )
    return FALSE ;

  /* ShadingType must be an integer in the range 1-7 */
  type = oInteger(*shadematch[shadematch_ShadingType].result) ;

  /* Initialise all matches to NULL, so we can test the dictmatch result
     without full checking of the ancestors */
  for ( i  = 0 ; i < NUM_ARRAY_ITEMS(shadematch1) ; ++i )
    shadematch1[i].result = NULL ;
  for ( i  = 0 ; i < NUM_ARRAY_ITEMS(shadematch23) ; ++i )
    shadematch23[i].result = NULL ;
  for ( i  = 0 ; i < NUM_ARRAY_ITEMS(shadematch4567) ; ++i )
    shadematch4567[i].result = NULL ;
  for ( i  = 0 ; i < NUM_ARRAY_ITEMS(datasource4567) ; ++i )
    datasource4567[i].result = NULL ;

  switch ( type ) {
  case 1:
    if ( !dictmatch(thed, shadematch1) )
      return FALSE ;
    break ;
  case 2: case 3:
    if ( !dictmatch(thed, shadematch23) )
      return FALSE ;
    break ;
  case 4: case 5: case 6: case 7:
    if ( !dictmatch(thed, shadematch4567 + (type != 5)) )
      return FALSE ;
    theo = shadematch4567[shadematch4567_DataSource].result ;
    if ( oType(*theo) == OSTRING ||
         oType(*theo) == OFILE ) { /* Need DataSource keys */
      if ( !dictmatch(thed, datasource4567 + (type == 5) ) )
        return FALSE ;
    }
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  return TRUE ;
}

/** Save linearised functions for recombine. Each patch of a shfill will create
   a merged representative colour from a sub-domain of this function. */
static Bool shading_recombine_function(SHADINGinfo *sinfo)
{
  HQASSERT(sinfo, "No shading info pointer") ;

  if ( sinfo->nfuncs > 0 ) {
    int32 i ;
    USERVALUE domain[4] ;
    OBJECT *fndomain = oArray(sinfo->fndecode) ;

    HQASSERT(theLen(sinfo->fndecode) == 2 || theLen(sinfo->fndecode) == 4,
             "Recombined shfill only supports 1 or 2-dimensional functions") ;
    HQASSERT(sinfo->nfuncs == 1,
             "Recombined shfill only supports one functions per separation") ;

    for ( i = 0 ; i < theLen(sinfo->fndecode) ; ++i ) {
      if ( !object_get_real(&fndomain[i], &domain[i]) )
        return FALSE ;
    }

    sinfo->rfuncs = (rcbs_function_h *)dl_alloc(sinfo->page->dlpools,
                                                RCBS_FUNC_HARRAY_SPACE(sinfo->nfuncs),
                                                MM_ALLOC_CLASS_SHADING) ;

    if ( sinfo->rfuncs == NULL )
      return error_handler(VMERROR) ;

    sinfo->rfcis = (COLORANTINDEX *)((uint8 *)sinfo->rfuncs +
                                     RCBS_FUNC_CINDEX_OFFSET(sinfo->nfuncs)) ;

    sinfo->rfcis[0] = rcbn_current_colorant() ;

    if ( !rcbs_fn_linearise(&sinfo->rfuncs[0], 0, domain, sinfo, 0) )
      return FALSE ;
  }

  return TRUE ;
}

/** Gouraud freeform triangle grid. Uses edge flag to determine which triangle
   follows from current one */
static Bool gouraud_freeform(SHADINGinfo *sinfo, SHADINGsource *sdata)
{
  SHADINGvertex *corners[4] ;
  int32 edgeflag = 0, nvert = 0, index ;
  int32 result = TRUE ;

  /* Set up pointer array which will be permuted. corners[3] is where we read
     the new vertex into, corners[0-2] contain the current Va, Vb, Vc. */
  if ( !vertex_alloc(corners, 4) )
    return error_handler(VMERROR) ;

  /* Don't yet know if Adobe allow empty DataSource, so err on the side of
     caution for now */
  while ( more_mesh_data(sdata) ) {
    if ( !get_vertex_freeform(sdata, corners[3], &edgeflag) ) {
      result = FALSE ;
      break ;
    }
    if ( nvert == 0 && edgeflag != 0 ) {
      result = error_handler(RANGECHECK) ;
      break ;
    }
    if ( nvert < 3 ) { /* Ignore edgeflag on 2nd & 3rd vertices */
      index = 0 ;       /* shuffle all down */
    } else if ( edgeflag == 0 ) {
      index = nvert = 0 ; /* shuffle all down */
    } else { /* Replace oldest or second oldest */
      index = edgeflag - 1 ;
    }
    do {
      SHADINGvertex *vtmp = corners[index] ;
      corners[index] = corners[index + 1] ;
      corners[index + 1] = vtmp ;
    } while ( ++index < 3 ) ;
    if ( ++nvert >= 3 ) {
      if ( sinfo->preseparated ) {
        if ( !shading_recombine_function(sinfo) ||
             !rcbs_store_gouraud(corners[0], corners[1], corners[2], sinfo) ) {
          result = FALSE ;
          break ;
        }
      } else if ( !decompose_triangle(corners[0], corners[1], corners[2], sinfo) ) {
        result = FALSE ;
        break ;
      }
    }
  }

  if ( result && (nvert == 1 || nvert == 2) )
    result = error_handler(RANGECHECK) ;

  vertex_free(sinfo, corners, 4) ;

  return result ;
}

/** Gouraud lattice. Pseudo-rectangular lattice of interpolated triangles. */
static Bool gouraud_lattice(SHADINGinfo *sinfo, SHADINGsource *sdata,
                             int32 vertperrow)
{
  int32 result = TRUE ;
  int32 rows = 0 ;
  SHADINGvertex **lat_curr, **lat_prev ;

  /* Allocate enough for two rows of vertices */
  if ( (lat_curr = (SHADINGvertex **)mm_alloc(mm_pool_shading,
                                              vertperrow * sizeof(SHADINGvertex *),
                                              MM_ALLOC_CLASS_SHADING)) == NULL ||
       (lat_prev = (SHADINGvertex **)mm_alloc(mm_pool_shading,
                                              vertperrow * sizeof(SHADINGvertex *),
                                              MM_ALLOC_CLASS_SHADING)) == NULL ||
       !vertex_alloc(lat_curr, vertperrow) ||
       !vertex_alloc(lat_prev, vertperrow) )
    return error_handler(VMERROR) ;

  /* Don't yet know if Adobe allow empty DataSource, so err on the side of
     caution for now */
  while ( result && more_mesh_data(sdata) ) {
    int32 index ;

    for ( index = 0 ; index < vertperrow ; ++index ) { /* get row of vertices */
      if ( !more_mesh_data(sdata) ) { /* Need full rows */
        result = error_handler(RANGECHECK) ;
        break ;
      }

      if ( !get_vertex_lattice(sdata, lat_curr[index]) ) {
        result = FALSE ;
        break ;
      }
    }

    /* If more than one row, add triangles between this and previous row */
    if ( result && ++rows > 1 ) {
      /* Note index starts at 1 to get right number of columns */
      for ( index = 1 ; index < vertperrow ; ++index ) {
#if defined( METRICS_BUILD )
        this_decomposition_depth = 0 ;
#endif
        if ( sinfo->preseparated ) {
          if ( !shading_recombine_function(sinfo) ||
               !rcbs_store_gouraud(lat_prev[index - 1], lat_prev[index],
                                   lat_curr[index - 1], sinfo) ||
               !rcbs_store_gouraud(lat_prev[index], lat_curr[index - 1],
                                   lat_curr[index], sinfo) ) {
            result = FALSE ;
            break ;
          }
        } else if ( !decompose_triangle(lat_prev[index - 1], lat_prev[index],
                                        lat_curr[index - 1], sinfo) ||
                    !decompose_triangle(lat_prev[index], lat_curr[index - 1],
                                        lat_curr[index], sinfo) ) {
          result = FALSE ;
          break ;
        }
      }
    }

    { /* Now swap vertex rows, so previously used one becomes current */
      SHADINGvertex **latt = lat_curr ;
      lat_curr = lat_prev ;
      lat_prev = latt ;
    }
  }

  if ( result && rows == 1 ) /* Need two or more rows to form lattice */
    result = error_handler(RANGECHECK) ;

  /* Could just let the pool destroy deal with these */
  vertex_free(sinfo, lat_curr, vertperrow) ;
  vertex_free(sinfo, lat_prev, vertperrow) ;
  mm_free(mm_pool_shading, lat_curr, sizeof(SHADINGvertex *) * vertperrow) ;
  mm_free(mm_pool_shading, lat_prev, sizeof(SHADINGvertex *) * vertperrow) ;

  return result ;
}

/** \defgroup samplesh Sampled shading
 *     \ingroup shfill */
/** \{ */

/** Shading Type 1: Sampled functions. Start with helper functions to
   interpolate in a specified component. Vertices must be specified
   in the order (x0,y0), (x1,y0), (x0,y1), (x1,y1) */
Bool shading_function_decompose(SHADINGvertex *v0, SHADINGvertex *v1,
                                SHADINGvertex *v2, SHADINGvertex *v3,
                                SHADINGinfo *sinfo, int cindex)
{
  SHADINGvertex *v[4] ;
  int32 index ;

  /* Limit recursion; when decomposed all components, we're finished */
  if ( cindex >= sinfo->ncomps )
    return (DEVICE_GOURAUD(sinfo->page, v0, v1, v2, sinfo) &&
            DEVICE_GOURAUD(sinfo->page, v1, v2, v3, sinfo)) ;

  HQASSERT(sinfo->ncomps == 2, "Number of function components not 2\n") ;
  HQASSERT(cindex >= 0 && cindex < sinfo->ncomps, "Component index out of range\n") ;
  HQASSERT((v0->comps[0] == v2->comps[0] && v1->comps[0] == v3->comps[0] &&
            v0->comps[1] == v1->comps[1] && v2->comps[1] == v3->comps[1]) ||
           (v0->comps[1] == v2->comps[1] && v1->comps[1] == v3->comps[1] &&
            v0->comps[0] == v1->comps[0] && v2->comps[0] == v3->comps[0]),
           "vertices not square in function axes\n") ;

  /* Re-arrange vertices so that we are interpolating edges 0..1 and 2..3.
     This code is not general enough to deal with any number of components
     because this rearrangement is specific to the 2 component case, but there
     is no need to deal with more components since that's what the 3010 spec
     allows. */
  if ( cindex != 0 ) {
    SHADINGvertex *vtmp = v2 ; v2 = v1 ; v1 = vtmp ;
  }

  v[0] = v0 ;
  v[1] = v1 ;
  v[2] = v2 ;
  v[3] = v3 ;

  /* Decompose along discontinuities for all output colour channels */
  for ( index = 0 ; index < sinfo->nfuncs ; ++index ) {
    SYSTEMVALUE weights[2] ;
    USERVALUE discont, bounds[2] ;
    USERVALUE c0 = v0->comps[cindex] ;
    USERVALUE c1 = v1->comps[cindex] ;
    int32 order ;

    /* It doesn't matter which way round we look, the discontinuity should
       still be there. So sort the bounds. */
    if ( c0 < c1 ) {
      bounds[0] = c0 ;
      bounds[1] = c1 ;
    } else if ( c0 > c1 ) {
      bounds[0] = c1 ;
      bounds[1] = c0 ;
    } else /* If bounds are same, there can't be a discontinuity */
      continue ;

    if ( !(sinfo->funcs ?
           fn_find_discontinuity(&sinfo->funcs[index], cindex, bounds,
                                 &discont, &order,
                                 FN_SHADING, index,
                                 sinfo->base_fuid, FN_GEN_NA,
                                 &sinfo->fndecode) :
           rcbs_fn_find_discontinuity(sinfo->rfuncs[index], cindex, bounds,
                                      &discont, &order)) )
      return FALSE ;

    if ( order != -1 ) {
      SHADINGvertex *vtmp[2] ;
      int32 result ;

      HQASSERT(discont > bounds[0] && discont < bounds[1],
               "Discontinuity not contained in range") ;

      /* Found discontinuity in this edge. It must intersect the opposite
         edge */
      weights[1] = (discont - c0) / (c1 - c0) ;
      weights[0] = 1 - weights[1] ;

      if ( !vertex_alloc(vtmp, 2) )
        return error_handler(VMERROR) ;

      vertex_interpolate(sinfo, 2, weights, vtmp[0], &v[0], sinfo->ncomps) ;

      /* Fix up rounding errors in discontinuity interpolation by
         performing relative error check for sanity, then forcing the
         colour value to the discontinuity. Rounding errors in position and
         other colour components may cause minor drift, but there's not
         much we can do about that. */
      HQASSERT(fabs((vtmp[0]->comps[cindex] - discont) / (c1 - c0)) < 1e-6,
               "Interpolation to discontinuity failed") ;
      vtmp[0]->comps[cindex] = discont ;
      HQASSERT(fabs(vtmp[0]->comps[1 - cindex] - v[0]->comps[1 - cindex]) / (c1 - c0) < 1e-6,
               "Interpolation affected other component") ;
      vtmp[0]->comps[1 - cindex] = v[0]->comps[1 - cindex] ;

      vertex_interpolate(sinfo, 2, weights, vtmp[1], &v[2], sinfo->ncomps) ;

      HQASSERT(fabs((vtmp[1]->comps[cindex] - discont) / (c1 - c0)) < 1e-6,
               "Interpolation to discontinuity failed") ;
      vtmp[1]->comps[cindex] = discont ;
      HQASSERT(fabs(vtmp[1]->comps[1 - cindex] - v[2]->comps[1 - cindex]) / (c1 - c0) < 1e-6,
               "Interpolation affected other component") ;
      vtmp[1]->comps[1 - cindex] = v[2]->comps[1 - cindex] ;

      result = (shading_function_decompose(v0, vtmp[0], v2, vtmp[1], sinfo, cindex) &&
                shading_function_decompose(vtmp[0], v1, vtmp[1], v3, sinfo, cindex)) ;

      vertex_free(sinfo, vtmp, 2) ;

      return result ;
    }
  }

  /* Recurse to decompose next component */
  return shading_function_decompose(v0, v1, v2, v3, sinfo, cindex + 1) ;
}

static Bool shading_function(SHADINGinfo *sinfo)
{
  int32 i, result ;
  OBJECT *theo ;
  SYSTEMVALUE domain[4] ;
  SHADINGvertex *corners[4] ;

  HQASSERT(sinfo, "Null shading info parameter in shading_function") ;

  /* Can't use function with Indexed space */
  if ( isIShadingInfoIndexed(sinfo) )
    return error_handler(RANGECHECK) ;

  /* Domain if present is an array of 4 numbers */
  if ( (theo = shadematch1[shadematch1_Domain].result) != NULL ) {
    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY,
             "Domain is not array in shading_function; is_shadingdict not called?") ;
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != 4 )
      return error_handler(RANGECHECK);

    /* Set up fndecode object to point at array */
    OCopy(sinfo->fndecode, *theo) ;

    theo = oArray(*theo);
    for ( i = 0 ; i < 4 ; ++i, ++theo ) {
      if ( !object_get_numeric(theo, &domain[i]) )
        return FALSE ;
    }

    /* Domain ordering: This doesn't appear to be explicitly specified in
       SUPP 3010, but is implicit in the definitions of domain in the
       function dictionaries, and is borne out by the Adobe implementation. */
    if ( domain[1] < domain[0] || domain[3] < domain[2] )
      return error_handler(RANGECHECK) ;
  } else { /* Set default domain */
    OCopy(sinfo->fndecode, default_decode) ;
    theLen(sinfo->fndecode) = 4 ;
    domain[0] = domain[2] = 0.0 ;
    domain[1] = domain[3] = 1.0 ;
  }

  /* Matrix if present is a transformation matrix */
  if ( (theo = shadematch1[shadematch1_Matrix].result) != NULL ) {
    OMATRIX m ;

    if ( !is_matrix(theo, &m) )
      return FALSE ;

    /* Modify gstate CTM by this matrix */
    gs_modifyctm(&m) ;
  }

  /* Check Function for correctness */
  sinfo->ncomps = 2 ; /* Always 2-input functions */
  sinfo->base_fuid = ++base_fuid ; /* Function IDs */

  theo = shadematch1[shadematch1_Function].result ;
  HQASSERT(theo &&
           (oType(*theo) == OARRAY ||
            oType(*theo) == OPACKEDARRAY ||
            oType(*theo) == OFILE ||
            oType(*theo) == ODICTIONARY),
           "Function is not an array or dictionary in shading_function; is_shadingdict not called?") ;

  if ( oType(*theo) == OARRAY ||
       oType(*theo) == OPACKEDARRAY ) { /* Array or packedarray */
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != sinfo->ncolors )
      return error_handler(RANGECHECK) ;

    /* Array of 2-in, 1-out functions */
    sinfo->nfuncs = sinfo->ncolors ;
    sinfo->funcs = oArray(*theo) ;
    sinfo->noutputs = 1 ;
  } else {
    /* Single 2-in, sinfo->ncolors out Function dict */
    sinfo->nfuncs = 1 ;
    sinfo->funcs = theo ;
    sinfo->noutputs = sinfo->ncolors ;
  }

  /* Typecheck the functions */
  for ( i = 0, theo = sinfo->funcs ; i < sinfo->nfuncs ; ++i, ++theo ) {
    if ( !is_functiondict(theo, sinfo, i) )
      return FALSE ;
  }

  if ( !vertex_pool_create(sinfo->ncomps) )
    return FALSE ;

  if ( !vertex_alloc(corners, 4) ) { /* Allocate all corners */
    vertex_pool_destroy() ;
    return error_handler(VMERROR) ;
  }

  for ( i = 0 ; i < 4 ; ++i ) {
    SYSTEMVALUE x = domain[i & 1], y = domain[(i >> 1) + 2] ;

    MATRIX_TRANSFORM_XY(x, y, corners[i]->x, corners[i]->y, &thegsPageCTM(*gstateptr)) ;

    corners[i]->comps[0] = (USERVALUE)x ;
    corners[i]->comps[1] = (USERVALUE)y ;

    corners[i]->opacity = 1.0;
  }

  /* Lock the functions */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_lock( FN_SHADING, i ) ;

  if ( sinfo->preseparated )
    result = (shading_recombine_function(sinfo) &&
              rcbs_store_function(corners, sinfo)) ;
#if defined( DEBUG_BUILD )
  else if ( (shading_debug_flag & SHADING_DEBUG_DISCONTINUITY) != 0 )
    result = (DEVICE_GOURAUD(sinfo->page, corners[0], corners[1], corners[2], sinfo) &&
              DEVICE_GOURAUD(sinfo->page, corners[1], corners[2], corners[3], sinfo)) ;
#endif
  else
    result = shading_function_decompose(corners[0], corners[1], corners[2],
                                        corners[3], sinfo, 0) ;

  /* Unlock the functions */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_unlock( FN_SHADING, i ) ;

  vertex_free(sinfo, corners, 4) ;
  vertex_pool_destroy() ;

  return result ;
}

/** \} */

/** Shading Types 2 & 3: Blends */
static Bool shading_blend(SHADINGinfo *sinfo)
{
  int32 i, ncoords, result ;
  OBJECT *theo ;
  SYSTEMVALUE coords[6] ;
  USERVALUE domain[2] ;
  USERVALUE opacity[2] ;
  int32 extend[2] ;

  HQASSERT(sinfo, "Null shading info parameter in shading_blend") ;

  ncoords = sinfo->type * 2 ;
  HQASSERT(ncoords == 4 || ncoords == 6,
           "Wrong number of coords in shading_blend") ;

  /* Can't use blend with Indexed space */
  if ( isIShadingInfoIndexed( sinfo ) )
    return error_handler(RANGECHECK) ;

  /* Coords is an array of 4 or 6 numbers */
  theo = shadematch23[shadematch23_Coords].result ;
  HQASSERT(theo && (oType(*theo) == OARRAY ||
                    oType(*theo) == OPACKEDARRAY),
           "Coords is not an array in shading_blend; is_shadingdict not called?") ;

  if ( !oCanRead(*theo) && !object_access_override(theo) )
    return error_handler(INVALIDACCESS) ;

  if ( theLen(*theo) != ncoords )
    return error_handler(RANGECHECK);

  theo = oArray(*theo);
  for ( i = 0 ; i < ncoords ; ++i, ++theo ) {
    if ( !object_get_numeric(theo, &coords[i]) )
      return FALSE ;
  }

  /* Domain if present is an array of 2 numbers */
  if ( (theo = shadematch23[shadematch23_Domain].result) != NULL ) {
    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY,
             "Domain is not an array in shading_blend; is_shadingdict not called?") ;

    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != 2 )
      return error_handler(RANGECHECK);

    /* Set up fndecode object to point at array */
    OCopy(sinfo->fndecode, *theo) ;

    theo = oArray(*theo);
    for ( i = 0 ; i < 2 ; ++i, ++theo ) {
      if ( !object_get_real(theo, &domain[i]) )
        return FALSE ;
    }

    /* Domain ordering: This doesn't appear to be explicitly specified in
       SUPP 3010, but is implicit in the definitions of domain in the
       function dictionaries, and is borne out by the Adobe implementation. */
    if ( domain[1] < domain[0] )
      return error_handler(RANGECHECK) ;
  } else { /* Set up default decode */
    OCopy(sinfo->fndecode, default_decode) ;
    theLen(sinfo->fndecode) = 2 ;
    domain[0] = 0.0f ;
    domain[1] = 1.0f ;
  }

  /* HqnOpacity, if present, is an array of two numbers or a function. */
  if ( (theo = shadematch23[shadematch23_HqnOpacity].result) != NULL ) {
    if ( oType(*theo) == OARRAY ||
         oType(*theo) == OPACKEDARRAY ) {
      /* The opacity value for a given point on the shfill is obtained by linearly
         interpolating between the two values given in this opacity array. */
      if ( !oCanRead(*theo) && !object_access_override(theo) )
        return error_handler(INVALIDACCESS) ;

      if ( theLen(*theo) != 2 )
        return error_handler(RANGECHECK);

      theo = oArray(*theo);
      for ( i = 0 ; i < 2 ; ++i, ++theo ) {
        if ( !object_get_real(theo, &opacity[i]) )
          return FALSE ;
      }
    } else {
      /* The opacity value for a given point on the shfill is obtained by
         running the opacity function from 0 to 1. */
      sinfo->opacity_func = theo ;
      opacity[0] = 0.0f ;
      opacity[1] = 1.0f ;
    }
  } else { /* By default the gradient is opaque */
    HQASSERT(sinfo->opacity_func == NULL, "opacity_func should have been initialised to NULL by now");
    opacity[0] = 1.0f ;
    opacity[1] = 1.0f ;
  }

  /* Check Function for correctness */
  sinfo->ncomps = 1 ; /* Always 1-input functions */
  sinfo->base_fuid = ++base_fuid ; /* Function IDs */

  theo = shadematch23[shadematch23_Function].result ;
  HQASSERT(theo &&
           (oType(*theo) == OARRAY ||
            oType(*theo) == OPACKEDARRAY ||
            oType(*theo) == OFILE ||
            oType(*theo) == ODICTIONARY),
           "Function is not an array or dictionary in shading_blend; is_shadingdict not called?") ;

  if ( oType(*theo) == OARRAY ||
       oType(*theo) == OPACKEDARRAY ) { /* Array or packedarray */
    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != sinfo->ncolors )
      return error_handler(RANGECHECK) ;

    /* Array of 1-in, 1-out functions */
    sinfo->nfuncs = sinfo->ncolors ;
    sinfo->funcs = oArray(*theo) ;
    sinfo->noutputs = 1 ;
  } else {
    /* Single 1-in, sinfo->ncolors out Function dict */
    sinfo->nfuncs = 1 ;
    sinfo->funcs = theo ;
    sinfo->noutputs = sinfo->ncolors ;
  }

  /* Typecheck the functions */
  for ( i = 0, theo = sinfo->funcs ; i < sinfo->nfuncs ; ++i, ++theo ) {
    if ( !is_functiondict(theo, sinfo, i) )
      return FALSE ;
  }

  /* Extend if present is an array of 2 booleans */
  if ( (theo = shadematch23[shadematch23_Extend].result) != NULL ) {
    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY,
             "Extend is not an array in shading_blend; is_shadingdict not called?") ;

    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != 2 )
      return error_handler(RANGECHECK);

    theo = oArray(*theo);
    for ( i = 0 ; i < 2 ; ++i, ++theo ) {
      if ( oType(*theo) != OBOOLEAN )
        return error_handler( TYPECHECK );
      extend[i] = oBool(*theo) ;
    }
  } else { /* Default is not to extend */
    extend[0] = extend[1] = FALSE ;
  }

  HQASSERT(sinfo->funcs, "No function objects") ;

  if ( !vertex_pool_create(sinfo->ncomps) )
    return FALSE ;

  /* Lock the functions */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_lock( FN_SHADING, i ) ;

  /* We have now finished extracting information from the shading dictionary.
     If we're in a preseparation phase, create a bogus DL object to contain
     this information, and store the sinfo data with it. Otherwise use the
     sinfo data to create shading sub-objects immediately. */
  if ( sinfo->preseparated )
    result = (shading_recombine_function(sinfo) &&
              rcbs_store_blend(coords, domain, extend, sinfo)) ;
  else if ( sinfo->type == 2 )
    result = axialblend(coords, domain, opacity, extend, sinfo) ;
  else
    result = radialblend(coords, domain, opacity, extend, sinfo) ;

  /* Unlock the functions */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_unlock( FN_SHADING, i ) ;

  vertex_pool_destroy() ;

  return result ;
}

/** Shading Types 4-7: Gouraud and Patch meshes. Lumped together because
   dictionary unpacking is similar. Decomposition is done by selection of
   function call at end of this function. */
static Bool shading_mesh(SHADINGinfo *sinfo)
{
  int32 i ;
  OBJECT *theo, *thes ;
  SYSTEMVALUE decode_array[12], *decode = decode_array ;
  SHADINGsource sdata ;
  int32 result = FALSE ;

  HQASSERT(sinfo, "Null shading info parameter in shading_mesh") ;

  /* Check Function for correctness. Do this first to decide if we have one
     component or more. */
  if ( (theo = shadematch4567[shadematch4567_Function].result) != NULL ) {
    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY ||
             oType(*theo) == OFILE ||
             oType(*theo) == ODICTIONARY,
             "Function is not an array or dictionary in shading_mesh; is_shadingdict not called?") ;

    if ( isIShadingInfoIndexed( sinfo ) )
      return error_handler(RANGECHECK) ;

    sinfo->ncomps = 1 ; /* Only one colour component allowed */
    sinfo->base_fuid = ++base_fuid ; /* Function IDs */

    if ( oType(*theo) == OARRAY ||
         oType(*theo) == OPACKEDARRAY ) { /* Array or packedarray */
      if ( !oCanRead(*theo) && !object_access_override(theo) )
        return error_handler(INVALIDACCESS) ;

      if ( theLen(*theo) != sinfo->ncolors )
        return error_handler(RANGECHECK) ;

      /* Array of 1-in, 1-out functions */
      sinfo->nfuncs = sinfo->ncolors ;
      sinfo->funcs = oArray(*theo) ;
      sinfo->noutputs = 1 ;
    } else {
      /* Single 1-in, sinfo->ncolors out Function dict */
      sinfo->nfuncs = 1 ;
      sinfo->funcs = theo ;
      sinfo->noutputs = sinfo->ncolors ;
    }
  } else { /* No functions */
    sinfo->ncomps = sinfo->ncolors ; /* Allocate space for base space colors */
    sinfo->nfuncs = 0 ;
    sinfo->noutputs = 0 ;
    sinfo->funcs = NULL ;
  }

  /* Examine DataSource */
  thes = shadematch4567[shadematch4567_DataSource].result ;
  HQASSERT(thes &&
           (oType(*thes) == OARRAY ||
            oType(*thes) == OPACKEDARRAY ||
            oType(*thes) == OFILE ||
            oType(*thes) == OSTRING),
           "DataSource is not an array, string or file in shading_mesh; is_shadingdict not called?") ;

  if ( !oCanRead(*thes) && !object_access_override(thes) )
    return error_handler(INVALIDACCESS) ;

  /* If DataSource is not array, require BitsPerCoordinate, BitsPerComponent,
     BitsPerFlag, Decode */
  if ( oType(*thes) == OSTRING ||
       oType(*thes) == OFILE ) {
    int32 ndecode = sinfo->ncomps * 2 + 4 ;
    int32 bitspercoord = 0, bitspercomp = 0, bitsperflag = 0 ;

    /* BitsPerFlag, but not in ShadingType 5 */
    if ( sinfo->type != 5 ) {
      theo = datasource4567[datasource4567_BitsPerFlag].result ;
      HQASSERT(theo && oType(*theo) == OINTEGER,
               "BitsPerFlag is not an integer in shading_mesh; is_shadingdict not called?") ;
      switch (oInteger(*theo)) {
      default:
        return error_handler(RANGECHECK) ;
      case 2: case 4: case 8:
        bitsperflag = oInteger(*theo) ;
        break ;
      }
    }

    /* BitsPerCoordinate */
    theo = datasource4567[datasource4567_BitsPerCoordinate].result ;
    HQASSERT(theo && oType(*theo) == OINTEGER,
             "BitsPerCooordinate is not an integer in shading_mesh; is_shadingdict not called?") ;
    switch (oInteger(*theo)) {
    default:
      return error_handler(RANGECHECK) ;
    case 1: case 2: case 4: case 8: case 12: case 16: case 24: case 32:
      bitspercoord = oInteger(*theo) ;
      break ;
    }

    /* BitsPerComponent */
    theo = datasource4567[datasource4567_BitsPerComponent].result ;
    HQASSERT(theo && oType(*theo) == OINTEGER,
             "BitsPerComponent is not an integer in shading_mesh; is_shadingdict not called?") ;
    switch (oInteger(*theo)) {
    default:
      return error_handler(RANGECHECK) ;
    case 1: case 2: case 4: case 8: case 12: case 16:
      bitspercomp = oInteger(*theo) ;
      break ;
    }

    /* Decode is an array of (n + 2) number pairs */
    theo = datasource4567[datasource4567_Decode].result ;
    HQASSERT(theo &&
             (oType(*theo) == OARRAY ||
              oType(*theo) == OPACKEDARRAY),
             "Decode is not an array in shading_mesh; is_shadingdict not called?") ;

    if ( !oCanRead(*theo) && !object_access_override(theo) )
      return error_handler(INVALIDACCESS) ;

    if ( theLen(*theo) != ndecode )
      return error_handler(RANGECHECK);

    if ( ndecode > 12 ) {
      SYSTEMVALUE *stmp =
        (SYSTEMVALUE *)mm_alloc(mm_pool_temp, ndecode * sizeof(SYSTEMVALUE),
                                MM_ALLOC_CLASS_SHADING) ;
      if ( stmp == NULL )
        return error_handler(VMERROR) ;
      decode = stmp ;
    }

    /* Set up fndecode object to point at component part of array */
    OCopy(sinfo->fndecode, *theo) ;
    oArray(sinfo->fndecode) += 4 ;
    theLen(sinfo->fndecode) -= 4 ;

    theo = oArray(*theo);
    for ( i = 0 ; i < ndecode ; ++i, ++theo ) {
      if ( !object_get_numeric(theo, &decode[i]) )
        goto shading_mesh_exit ;
    }

    if ( oType(*thes) == OFILE ) {
      FILELIST *flptr = oFile(*thes) ;

      if ( sinfo->inpattern2 && !isIRewindable(flptr) ) {
        result = error_handler(IOERROR) ;
        goto shading_mesh_exit ;
      }

      if ( !datasource_stream(sinfo, &sdata, thes, bitspercomp,
                              bitspercoord, bitsperflag, decode) )
        goto shading_mesh_exit ;
    } else {
      HQASSERT(oType(*thes) == OSTRING,
               "DataSource type not known" ) ;
      if ( !datasource_string(sinfo, &sdata, thes, bitspercomp,
                              bitspercoord, bitsperflag, decode) )
        goto shading_mesh_exit ;
    }
  } else { /* DataSource is an array */
    OCopy(sinfo->fndecode, default_decode) ; /* No decode object */
    theLen(sinfo->fndecode) = 2 ;

    /* Do we need to produce errors if Decode are BitsPer* are set? */
    if ( !datasource_array(sinfo, &sdata, thes) )
      goto shading_mesh_exit ;
  }

  /* Typecheck the functions */
  for ( i = 0, theo = sinfo->funcs ; i < sinfo->nfuncs ; ++i, ++theo ) {
    if ( !is_functiondict(theo, sinfo, i) )
      goto shading_mesh_exit ;
  }

  /* Lock the functions. Do not combine this with the previous loop, it
     needs to be atomic for error recovery. */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_lock( FN_SHADING, i ) ;

  if ( !vertex_pool_create(sinfo->ncomps) )
    goto shading_mesh_unlock ;

  if ( sinfo->preseparated ) {
    if ( !shading_recombine_function(sinfo) )
      goto shading_mesh_unlock ;
  }

  /* Consume data, creating mesh vertices */
  switch ( sinfo->type ) {
  case 4:
    result = gouraud_freeform(sinfo, &sdata) ;
    break ;
  case 5:
    /* VerticesPerRow is required for shading type 5 */
    theo = shadematch4567[shadematch4567_VerticesPerRow].result ;
    if ( oInteger(*theo) < 2 ) {
      result = error_handler(RANGECHECK) ;
    } else {
      result = gouraud_lattice(sinfo, &sdata, oInteger(*theo)) ;
    }
    break ;
  case 6:
  case 7:
    result = tensor_mesh(sinfo, &sdata, sinfo->type) ;
    break ;
  default:
    result = error_handler(UNREGISTERED) ;
    break ;
  }

shading_mesh_unlock:
  vertex_pool_destroy() ;

  /* Unlock the functions */
  for ( i = 0; i < sinfo->nfuncs; i++ )
    fn_unlock( FN_SHADING, i ) ;

shading_mesh_exit:
  if ( decode != decode_array ) {
    mm_free(mm_pool_temp, (mm_addr_t)decode, (sinfo->ncomps * 2 + 4) * sizeof(SYSTEMVALUE)) ;
  }
  return result ;
}

/* Draw background rectangle; optimise by drawing rect over clipping area.
   Note this probably isn't correct for patterns inside patterns, since the
   clipping rectangle here may overlap more than one replication area. I'm not
   sure yet whether this situation can arise. */
static Bool fill_clipping_area(DL_STATE *page)
{
  SYSTEMVALUE x1, y1, x2, y2 ;
  dbbox_t rectfill ;

  x1 = theX1Clip(thegsPageClip(*gstateptr)) ;
  x2 = theX2Clip(thegsPageClip(*gstateptr)) ;
  y1 = theY1Clip(thegsPageClip(*gstateptr)) ;
  y2 = theY2Clip(thegsPageClip(*gstateptr)) ;

  if ( isHDLTEnabled( *gstateptr )) {
    path_fill_four(x1, y1, x1, y2, x2, y2, x2, y1) ;

    switch ( IDLOM_FILL( GSC_SHFILL , NZFILL_TYPE , & i4cpath , NULL )) {
    case NAME_false:            /* PS error in IDLOM callbacks */
      return FALSE ;
    case NAME_Discard:          /* just pretending */
      return TRUE ;
    default:                    /* only add, for now */
      ;
    }
  }

  if ( degenerateClipping )
    return TRUE ;

  /* Need to convert to device coords before we know if we'll fit in a rect */
  SC_C2D_INT( rectfill.x1, x1) ;
  SC_C2D_INT( rectfill.y1, y1) ;
  SC_C2D_INT( rectfill.x2, x2) ;
  SC_C2D_INT( rectfill.y2, y2) ;

  if ( !rcbn_enabled() )
    return DEVICE_RECT(page, &rectfill);
  else { /* Use a normal fill structure */
    NFILLOBJECT *nfill ;

    if ( ! isHDLTEnabled( *gstateptr )) /* Done already if IDLOM was needed */
      path_fill_four(x1, y1, x1, y2, x2, y2, x2, y1) ;

    if ( make_nfill(page,  &p4cpath , NFILL_ISRECT, &nfill) )
      return DEVICE_BRESSFILL(page, ISRECT|NZFILL_TYPE, nfill) ;
  }

  return FALSE ;
}

Bool gs_shfill(STACK *stack, GSTATE *pgstate, int32 flags)
{
  corecontext_t *context = get_core_context_interp();
  int32 result = FALSE ;
  int32 stacksize, gid;
  Bool presep ;
  Bool pdfout_was_enabled = FALSE;
  OBJECT *thed, *theo, rsdo ;
  RECTANGLE rect;
  SHADINGinfo sinfo ;
  USERVALUE scratch4[4] ;
  Bool device_setg_needed = TRUE ;
  Bool enable_recombine_interception = FALSE ;
  LISTOBJECT *lobj ;
  USERVALUE noise ;
  uint16 noisesize ;

  HQASSERT( stack , "stack is null in gs_shfill" ) ;
  /* This is a restriction imposed by many assumptions in this file */
  HQASSERT(pgstate == gstateptr, "gs_shfill requires gstateptr ATM");

  sinfo.page = context->page ;
  sinfo.scratch = scratch4 ;
  sinfo.opacity_func = NULL ;
  sinfo.rfuncs = NULL ;
  sinfo.rfcis = NULL ;
  sinfo.base_addr = &sinfo ;

  if ( DEVICE_INVALID_CONTEXT() )
    return error_handler(UNDEFINED);

  if ( (flags & GS_SHFILL_VIGNETTE) == 0 )
    if ( ! flush_vignette( VD_Default ))
      return FALSE ;

  stacksize = theIStackSize(stack) ;
  if ( stacksize < 0 )
    return error_handler(STACKUNDERFLOW) ;

  if ( !optional_content_on ||
       CURRENT_DEVICE_SUPPRESSES_MARKS() ||
       char_doing_charpath() ) {
    pop(stack) ;
    return TRUE ;
  }

  thed = TopStack(*stack, stacksize) ;

  /* Extract information from shading dict and dispatch to shading
     function */

  HQASSERT(thed, "Null dictionary object in gs_shfill") ;

  if ( !is_shadingdict(thed) )
    return FALSE ;

  /* Extract appropriate values for keys */
  HQASSERT(shadematch[shadematch_ShadingType].result != NULL,
           "ShadingType NULL in gs_shfill; is_shadingdict not called?") ;
  sinfo.type = oInteger(*shadematch[shadematch_ShadingType].result) ;

  if ( !gs_gpush( GST_SHADING ))
    return FALSE ;
#define return DO_NOT_RETURN_-_goto_shfill_exit_INSTEAD!

  gid = gstackptr->gId ;

  if ( !runHooks (& gstateptr->thePDEVinfo.pagedevicedict, GENHOOK_StartVignette))
    goto shfill_exit ;

  if ( !gs_gpush( GST_GSAVE ))
    goto shfill_exit ;

  pgstate = gstateptr ;

  /* ColorSpace must be name or array */
  HQASSERT(shadematch[shadematch_ColorSpace].result != NULL,
           "ColorSpace NULL in gs_shfill; is_shadingdict not called?") ;

  if ( !push(shadematch[shadematch_ColorSpace].result, stack) )
    goto shfill_exit ;
  if ( ! gsc_setcolorspace(pgstate->colorInfo, stack, GSC_SHFILL) ) {
    pop(stack) ; /* pop colorspace */
    goto shfill_exit ;
  }

  if ( gsc_chainCanBeInvoked( pgstate->colorInfo, GSC_SHFILL_INDEXED_BASE )) {
    sinfo.base_index = GSC_SHFILL_INDEXED_BASE ;
  }
  else {
    sinfo.base_index = GSC_SHFILL ;
  }

  if (gsc_getcolorspace(pgstate->colorInfo, sinfo.base_index) == SPACE_Pattern) {
    (void) error_handler(TYPECHECK) ; /* As on Laserwriter 8010 */
    goto shfill_exit ;
  }

  sinfo.ncolors = gsc_dimensions( pgstate->colorInfo, sinfo.base_index ) ;

  if ( sinfo.ncolors > 4 ) {
    USERVALUE *utmp =
      (USERVALUE *)mm_alloc(mm_pool_temp, sizeof(USERVALUE) * sinfo.ncolors,
                            MM_ALLOC_CLASS_SHADING) ;
    if ( utmp == NULL ) {
      (void)error_handler(VMERROR) ;
      goto shfill_exit ;
    }
    sinfo.scratch = utmp ;
  }
  sinfo.spotno = gsc_getSpotno(pgstate->colorInfo) ;

  if ( !gsc_isPreseparationChain(pgstate->colorInfo, sinfo.base_index, &presep) )
    goto shfill_exit ;

  sinfo.preseparated = (uint8)presep;

  if ( !presep ) {
    /* Disable recombine interception for shfills not being handled as
       preseparated. */
    rcbn_disable_interception(pgstate->colorInfo);
    enable_recombine_interception = TRUE;
  }

  {
    GUCR_RASTERSTYLE *targetRS = gsc_getTargetRS(pgstate->colorInfo);
    GUCR_RASTERSTYLE *deviceRS = gsc_getRS(pgstate->colorInfo);
    int32 htmax = 0 ;
    double smoothness ;

    /* Smoothness control. Store smoothness in shading object as the number
       of bands required to achieve the requested smoothness setting.  This
       number is capped at max levels.

       This sets the minimum number of bands for the smoothness. The actual
       number is chosen independently for each colorant at render time,
       depending on the number of halftone levels available. (It could be set
       here, but that would require storing the numbers of levels for each
       colorant. The computation of the exact number of levels is not very
       intensive. The number of bands is limited to the maximum of the number
       of halftone levels available.)

       The reason that 0.5/smoothness is used instead of 1.0/smoothness is
       that the smoothness is defined as the maximum allowable error in each
       colour component. Each band can cover the range -smoothness to
       +smoothness from a particular colour. Since the end colours are
       covered by half-ranges, there should actually be a +1 on this
       expression, but there is a compensating off-by-one in the calculation
       of the number of bands, where the maximum halftone value is used
       rather than the number of halftone values. */

    if ( presep ) {
      /* Cannot be sure what this separation is, so use default. */
      htmax = ht_getClear(sinfo.spotno, REPRO_TYPE_VIGNETTE,
                          COLORANTINDEX_NONE, deviceRS);
    } else {
      COLORANTINDEX *iColorants ;
      int32 nColorants, i ;

      if ( !gsc_getDeviceColorColorants(pgstate->colorInfo, sinfo.base_index,
                                        &nColorants, &iColorants) )
        goto shfill_exit ;

      /* Map the colorants onto device colorants and find max halftone level. */
      for ( i = 0; i < nColorants; i++ ) {
        COLORANTINDEX* cimap;

        /* Map the ci from the current rasterstyle onto its equivalent in the
           real device rastersyle. */
        if (iColorants[i] != COLORANTINDEX_NONE && iColorants[i] != COLORANTINDEX_ALL &&
            guc_equivalentRealColorantIndex(targetRS, iColorants[i], & cimap)) {
          do {
            COLORVALUE clear = ht_getClear(sinfo.spotno, REPRO_TYPE_VIGNETTE,
                                           cimap[0], deviceRS);
            if ( htmax < clear )
              htmax = clear;
            ++cimap;
          } while (*cimap != COLORANTINDEX_UNKNOWN);
        } else {
          /* iColorants[i] must be a virtual colorant (i.e. not being output
             on the device and therefore will eventually be converted to process),
             or the None/All colorant, indicating we should use the default screen. */
          COLORVALUE clear = ht_getClear(sinfo.spotno, REPRO_TYPE_VIGNETTE,
                                         COLORANTINDEX_NONE, deviceRS);
          if ( htmax < clear )
            htmax = clear;
        }
      }
    }

    HQASSERT(htmax >= 0 && htmax <= COLORVALUE_MAX,
             "Maximum halftone levels out of range") ;
    sinfo.smoothnessbands = (uint16)htmax ;

    smoothness = gs_currsmooth(context, pgstate) ;
    if ( smoothness > 0.0 ) {
      smoothness = ceil(0.5 / smoothness) ;
      if ( smoothness < sinfo.smoothnessbands )
        sinfo.smoothnessbands = (uint16)smoothness ;
    }
  }

  sinfo.inpattern2 = (uint8)((flags & GS_SHFILL_PATTERN2) != 0) ;

  /* BBox if present is an array of four numbers. Transform BBox into x, y, w,
     h and perform rectclip. After that we can treat clip region as area to
     fill */
  if ( (theo = shadematch[shadematch_BBox].result) != NULL ) {
    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY,
             "BBox is not an array in gs_shfill; is_shadingdict not called?") ;

    if ( !oCanRead(*theo) && !object_access_override(theo) ) {
      (void)error_handler(INVALIDACCESS) ;
      goto shfill_exit ;
    }

    if ( !object_get_bbox(theo, &sinfo.bbox) )
      goto shfill_exit ;

    bbox_normalise(&sinfo.bbox, &sinfo.bbox);

    bbox_to_rectangle(&sinfo.bbox, &rect) ;

    if ( rect.w < 0 || rect.h < 0 ) {
      (void)error_handler(RANGECHECK);
      goto shfill_exit ;
    }
  } else { /* Transform clipping area to userspace BBox */
    SET_SINV_SMATRIX(&thegsPageCTM(*pgstate), NEWCTM_ALLCOMPONENTS) ;
    if ( SINV_NOTSET(NEWCTM_ALLCOMPONENTS) ) {
      (void)error_handler(UNDEFINEDRESULT) ;
      goto shfill_exit ;
    }

    bbox_store(&sinfo.bbox,
               theX1Clip(thegsPageClip(*pgstate)),
               theY1Clip(thegsPageClip(*pgstate)),
               theX2Clip(thegsPageClip(*pgstate)),
               theY2Clip(thegsPageClip(*pgstate))) ;

    bbox_transform(&sinfo.bbox, &sinfo.bbox, &sinv) ;
  }

  /* AntiAlias, if present, is a boolean. But ensure it obeys the condition
     set by the 'ShadingAntiAliasDefault' user parameter. */
  sinfo.antialias =
    (uint8)quadStateApplyToObject(&context->userparams->ShadingAntiAliasDefault,
                                  shadematch[shadematch_AntiAlias].result);

  if ( sinfo.antialias ) {
    uint32 gridsize =  (uint32)((sinfo.page->xdpi < sinfo.page->ydpi
                                 ? sinfo.page->xdpi : sinfo.page->ydpi)
                                * context->userparams->ShadingAntiAliasSize / 72.0) + 1 ;
    noise = context->userparams->ShadingAntiAliasFactor ;

    /* Noise grid mask indicates size of grid on which noise is applied. */
    if ( gridsize >= 0x10000u )
      noisesize = 16 ;
    else if ( gridsize >= 0x100 ) {
      noisesize = CAST_TO_UINT16(highest_bit_set_in_byte[gridsize >> 8] + 8);
    } else if ( gridsize != 0 ) {
      noisesize = (uint16)highest_bit_set_in_byte[gridsize];
    } else
      noisesize = 0 ;
  } else {
    noise = 0.0f ;
    noisesize = 0 ;
  }

  /* Background if present is an array of colour values. Unpack it to
     stack, and set it as current colour. This can then be used to draw
     BBox/clipping rect background. Background is ignored in shfill
     (Adobe Supplement 3010 p.206) */
  if ( (flags & GS_SHFILL_PATTERN2) != 0 &&
       (theo = shadematch[shadematch_Background].result) != NULL ) {
    int32 i, ncolors = gsc_dimensions( pgstate->colorInfo, GSC_SHFILL ) ;

    HQASSERT(oType(*theo) == OARRAY ||
             oType(*theo) == OPACKEDARRAY,
             "Background is not an array in gs_shfill; is_shadingdict not called?") ;

    if ( !oCanRead(*theo) && !object_access_override(theo) ) {
      (void)error_handler(INVALIDACCESS) ;
      goto shfill_exit ;
    }

    if ( theLen(*theo) != ncolors ) {
      (void)error_handler(RANGECHECK);
      goto shfill_exit ;
    }

    theo = oArray(*theo);
    for ( i = 0 ; i < ncolors ; ++i, ++theo ) {
      switch ( oType(*theo) ) {
      case OINTEGER:
      case OREAL:
        if ( !push(theo, stack) ) {
          npop(i, &operandstack) ;
          goto shfill_exit ;
        }
        break ;
      default:
        npop(i, stack) ;
        (void)error_handler( TYPECHECK );
        goto shfill_exit ;
      }
    }

    if ( !gsc_setcolor(pgstate->colorInfo, stack, GSC_SHFILL) ) {
      npop(i, stack) ; /* Pop colour values */
      goto shfill_exit ;
    }
  }

  /* HCMS should treat this like a vignette */
  if (!gsc_setRequiredReproType( pgstate->colorInfo, GSC_SHFILL,
                                 REPRO_TYPE_VIGNETTE ))
      goto shfill_exit ;

  sinfo.coercion = NAME_Shfill ; /* i.e., not coerced */

  if ( isHDLTEnabled( *pgstate )) {
    /* HDLT needs DEVICE_SETG before callback to setup clipping and states
       correctly. We may need to repeat the device_setg if a BBox was used to
       clip the shfill, since the bbox clipping is not yet in effect. */
    if ( !DEVICE_SETG(sinfo.page, GSC_SHFILL, DEVICE_SETG_NORMAL) )
      goto shfill_exit ;

    device_setg_needed = FALSE ;

    switch ( IDLOM_SHFILL( GSC_SHFILL, &sinfo, thed ) ) {
    case NAME_false:    /* PS error in IDLOM callbacks */
      goto shfill_exit ;
    case NAME_Discard:  /* just pretending */
      result = TRUE ;
      goto shfill_exit ;
    case NAME_Fill:     /* Coercions */
      sinfo.coercion = NAME_Fill ;
      break ;
    case NAME_Gouraud:
      sinfo.coercion = NAME_Gouraud ;
      break ;
    default:
      HQFAIL("HDLT callback returned bad value") ;
      /*@fallthrough@*/
    case NAME_Add:      /* only Add, for now */
      /* Since we're adding the shfill and not coercing it, turn off HDLT for
         any objects produced by the shading_* functions. This is OK because
         we're in a gsave/grestore context anyway. */
      theIdlomState(*pgstate) = HDLT_DISABLED ;
      break ;
    }

    if ( degenerateClipping ) {
      result = TRUE ;
      goto shfill_exit ;
    }
  }

  if (pdfout_enabled()) {
    OBJECT *dsrc = shadematch4567[shadematch4567_DataSource].result ;

    if ( dsrc && oType(*dsrc) == OFILE ) {
      /* If the shading has a file datasource, then layer an RSD on top of it
         and extract the data, so that both PDF out and PostScript can
         access the same information. */
      FILELIST *flptr = oFile(*dsrc) ;

      HQASSERT(flptr, "DataSource file NULL") ;

      if ( !isIRewindable(flptr) ) {
        if ( !filter_layer_object(dsrc,
                                  NAME_AND_LENGTH("ReusableStreamDecode"),
                                  NULL, &rsdo) )
          goto shfill_exit ;

        shadematch4567[shadematch4567_DataSource].result = dsrc = &rsdo ;
      }
    }

    /* Need to flag that PDF out was originally turned on. */
    pdfout_was_enabled = TRUE;
    if (! pdfout_doshfill(context->pdfout_h, thed, dsrc))
      goto shfill_exit ;
  }

  /* Yes, we really mean sinfo.ncolors here; this is the number of colours
     the (Indexed) space translates to. */
  if ( new_color_detected && sinfo.ncolors > 1 ) {
    if (!detect_setcolor_separation())  /* flag page as composite */
      goto shfill_exit;
  }

  /* Clip to bbox extracted from shading dict. Must be done before SETG to get
     clipping set up correctly. */
  if ( (theo = shadematch[shadematch_BBox].result) != NULL ) {
    if ( !cliprectangles(&rect, 1) )
      goto shfill_exit ;
    device_setg_needed = TRUE ; /* Need to re-do setg, even if done for HDLT */
  }

  /* Do this before painting background */
  if ( device_setg_needed ) {
    if ( !DEVICE_SETG(sinfo.page, GSC_SHFILL, DEVICE_SETG_NORMAL) )
      goto shfill_exit ;

    if ( degenerateClipping ||
         dlc_is_none(dlc_currentcolor(sinfo.page->dlc_context)) ) {
      result = TRUE ;
      goto shfill_exit ;
    }
  }

  /* For Background */
  DISPOSITION_STORE(dl_currentdisposition, REPRO_TYPE_OTHER, GSC_SHFILL,
                    gstateptr->user_label ? DISPOSITION_FLAG_USER : 0);

  /* Draw previously prepared background */
  if ( (flags & GS_SHFILL_PATTERN2) != 0 &&
       (theo = shadematch[shadematch_Background].result) != NULL ) {
    if ( ! fill_clipping_area(sinfo.page))
      goto shfill_exit ;
  }

  DISPOSITION_STORE(dl_currentdisposition, REPRO_TYPE_VIGNETTE, GSC_SHFILL,
                    gstateptr->user_label ? DISPOSITION_FLAG_USER : 0);


  /* Now that setg has been called, we know the lateColorAttrib which is required
     for probing transparency groups to enable decomposition in output space */
  sinfo.lca = sinfo.page->currentdlstate->lateColorAttrib;

  ht_defer_allocation() ;

  if ( make_listobject(sinfo.page, RENDER_shfill, NULL, &lobj) ) {
    dbbox_t bbox;

    /* By this time, the clipping area is limited to the maximum extent of
       the shfill. We create a banded DL even when preseparated, using the
       clipping area as a bounding box. */
    bbox = cclip_bbox ;

    if ( setup_shfill_dl(sinfo.page, lobj) ) {
      switch ( sinfo.type ) {
      case 1:
        result = shading_function(&sinfo) ;
        break ;
      case 2: case 3:
        result = shading_blend(&sinfo) ;
        break ;
      case 4: case 5: case 6: case 7:
        result = shading_mesh(&sinfo) ;
        break ;
      default:
        result = error_handler(RANGECHECK) ;
      }

      /* reset_shfill_dl must be called to tidy up: */
      result = reset_shfill_dl(sinfo.page, &sinfo, result,
                               sinfo.smoothnessbands,
                               noise, noisesize, &bbox);
    }

    if ( result ) {
      bbox_intersection(&bbox, &cclip_bbox, &bbox);

      /* If preseparating, store SHADINGinfo and matrix */
      if ( presep && !bbox_is_empty(&bbox) ) {
        SHADINGinfo *saveinfo = alloc_SHADINGinfo(sinfo.page);
        void *save_addr = saveinfo->base_addr ;

        if ( saveinfo != NULL ) {
          OMATRIX *savematrix = (OMATRIX *)(saveinfo + 1) ;
          dbbox_t *savebbox = (dbbox_t *)(savematrix + 1) ;

          *saveinfo = sinfo ;

          /* Don't leave dangling references */
          saveinfo->base_addr = save_addr ;
          saveinfo->scratch = NULL ;
          saveinfo->funcs = NULL ;
          saveinfo->opacity_func = NULL ;
          oArray(saveinfo->fndecode) = default_decode_olist ;

          /* Transfer ownership of recombined functions to saved info */
          sinfo.rfuncs = NULL ;
          sinfo.rfcis = NULL ;

          /* Indexed colorspaces are reconstructed as base space */
          saveinfo->base_index = GSC_SHFILL ;

          /* Flatness is saved for reconstruction */
          saveinfo->rflat = theFlatness(theLineStyle(*pgstate)) ;

          /* Save matrix for recombine */
          MATRIX_COPY(savematrix, &thegsPageCTM(*pgstate)) ;

          /* Save patch bbox */
          *savebbox = bbox ;

          lobj->dldata.shade->info = saveinfo;
        } else
          result = error_handler(VMERROR) ;
      }
    }

    lobj->bbox = bbox;
    /* Store accumulated overprint set in top-level DL object */
    if ( result && !bbox_is_empty(&bbox) )
      result = add_listobject(sinfo.page, lobj, NULL);
    else
      free_dl_object(lobj, sinfo.page) ;
  }

  ht_resume_allocation(sinfo.spotno, result) ;

shfill_exit:

  if ( !runHooks (& gstateptr->thePDEVinfo.pagedevicedict, GENHOOK_EndVignette))
    result = FALSE ;

  if (pdfout_was_enabled) {
    if ( shadematch4567[shadematch4567_DataSource].result == &rsdo ) {
      /* If we created an RSD, close it */
      FILELIST *flptr = oFile(rsdo) ;

      (void)(*theIMyCloseFile(flptr))(flptr, CLOSE_EXPLICIT) ;
    }

    pdfout_endshfill(context->pdfout_h);
  }

  if ( result )
    pop(stack) ;

  if ( enable_recombine_interception )
    rcbn_enable_interception(pgstate->colorInfo);

  if ( !gs_cleargstates(gid, GST_SHADING, NULL))
    result = FALSE ;

  if ( sinfo.scratch != scratch4 ) {
    mm_free(mm_pool_temp, (mm_addr_t)sinfo.scratch, sizeof(USERVALUE) * sinfo.ncolors) ;
  }

  if ( sinfo.rfuncs ) { /* Tidy up after failed recombination attempt */
    dl_free(sinfo.page->dlpools, (mm_addr_t)sinfo.rfuncs,
            RCBS_FUNC_HARRAY_SPACE(sinfo.nfuncs), MM_ALLOC_CLASS_SHADING);
  }

#undef return
  return result ;
}


static SHADINGinfo *alloc_SHADINGinfo(DL_STATE *page)
{
  SHADINGinfo *result;
  void *base = dl_alloc(page->dlpools,
      sizeof(SHADINGinfo) + sizeof(OMATRIX) + sizeof(dbbox_t) + 4,
      MM_ALLOC_CLASS_SHADING);

  if (base == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  result = (SHADINGinfo *)DWORD_ALIGN_UP(uintptr_t, base);
  HQASSERT(DWORD_IS_ALIGNED(uintptr_t, result),
      "SHADINGinfo is not 8-byte aligned");

  result->base_addr = base;

  return result;
}

/* ----------------------------------------------------------------------------
Log stripped */
