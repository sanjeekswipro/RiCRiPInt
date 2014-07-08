/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gscicc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Constructor for an iccbased link in a color chain.
 */

#include "core.h"

#include "blobdata.h"           /* blob_from_file */
#include "bloberrors.h"         /* error_from_sw_blob_result */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "display.h"            /* LateColorAttrib */
#include "dl_store.h"           /* lateColorAttribNew */
#include "dlstate.h"            /* DL_STATE */
#include "fileio.h"             /* FILELIST */
#include "gcscan.h"             /* ps_scan_field */
#include "group.h"              /* groupParent */
#include "hdl.h"                /* hdlGroup */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "matrix.h"             /* matrix_inverse_3x3 */
#include "miscops.h"            /* run_ps_string */
#include "mm_core.h"            /* mm_pool_color */
#include "lowmem.h"             /* low_mem_handler_t */
#include "monitor.h"            /* monitorf */
#include "mps.h"                /* mps_root_t */
#include "namedef_.h"           /* NAME_* */
#include "objects.h"            /* oType */
#include "preconvert.h"         /* preconvert_invoke_all_colorants */
#include "swcmm.h"              /* SW_CMM_INTENT_PERCEPTUAL */
#include "swerrors.h"           /* TYPECHECK */

#include "gs_colorpriv.h"       /* CLINK */
#include "gschcmspriv.h"        /* cc_convertIntentToName */
#include "gschead.h"            /* gsc_getcolorspacesizeandtype  */
#include "gscparamspriv.h"      /* colorUserParams */
#include "icmini.h"             /* DATA_ACTION */

#include "gsciccpriv.h"

struct ps_context_t;

/*----------------------------------------------------------------------------*/

typedef Bool (*CONSTRUCTOR_FUNCTION) (CLINKiccbased **invoke_ptr,
                                      FILELIST* flptr,
                                      int32 num_tags,
                                      icTags *tags,
                                      icTagSignature desc_sig);

#define INVALID_INTENT    ((uint8) (-1))

/* The max number of ICC input/output rendering tables per profile. */
#define N_ICC_RENDERING_TABLES            (3)

static mps_root_t ICCCacheRoot;

/*----------------------------------------------------------------------------*/
/* Following taken from iclow.c */

/*
 * A collection of functions that fix (swap the byte order if necessary) of
 * various structures. The individual fields are fixed by macros defined
 * elsewhere.
 */

static void iccfix_64(icUInt64Number *p)
{
  icfixu32((*p)[0]);
  icfixu32((*p)[1]);
}

static void iccfix_date(icDateTimeNumber *p)
{
  icfixu16(p->year);
  icfixu16(p->month);
  icfixu16(p->day);
  icfixu16(p->hours);
  icfixu16(p->minutes);
  icfixu16(p->seconds);
}

static void iccfix_xyz(icXYZNumber *p)
{
  icfixs15f16(p->X);
  icfixs15f16(p->Y);
  icfixs15f16(p->Z);
}

static void iccfix_header(icHeader *p)
{
  icfixu32(p->size);
  icfixsig(p->cmmId);
  icfixu32(p->version);
  icfixsig(p->deviceClass);
  icfixsig(p->colorSpace);
  icfixsig(p->pcs);
  iccfix_date(&p->date);
  icfixsig(p->magic);
  icfixsig(p->platform);
  icfixu32(p->flags);
  icfixsig(p->manufacturer);
  icfixu32(p->model);
  iccfix_64(&p->attributes);
  icfixu32(p->renderingIntent);
  iccfix_xyz(&p->illuminant);
}

#ifdef MAY_BE_NEEDED
static void iccfix_scrn(icScreeningData *p)
{
  icfixs15f16(p->frequency);
  icfixs15f16(p->angle);
  icfixsig(p->spotShape);
}
#endif

static void iccfix_tag(icTag *p)
{
  icfixsig(p->sig);
  icfixu32(p->offset);
  icfixu32(p->size);
}

/*----------------------------------------------------------------------------*/
/* Following adapted from icolor.c */

/* Find a tag by name and return a ptr to that tag. If not found, return 0. */
static icTags *findTag( icTagSignature tag_name, int32 num_tags, icTags *tags )
{
  int32   i;
  icTags  *p;

  p = tags;

  for ( i = 0; i < num_tags; ++i,++p ) {
    if ( p->tag.sig == tag_name )
      return p ;
  }

  return 0;
}

/*----------------------------------------------------------------------------*/
/* seek on a file or filter */

static Bool mi_seek( FILELIST* file, int32 offset )
{
  Hq32x2 file_offset;
  int32 result;

  if ( !isIOpenFile(file) )
    return error_handler(IOERROR);

  /* fake a 64bit file offset */
  Hq32x2FromInt32(&file_offset, offset) ;

#ifdef IC_NO_RSD /* WARNING! untested code */
  if ( isIRealFile(file) || isIRSDFilter(file) ) {
#endif /* end of warning */

    /* this is a seekable file */

    result = isIOutputFile(file) ? (*theIMyFlushFile(file))(file)
                                 : (*theIMyResetFile(file))(file);
    if ( result == EOF ||
         (*theIMySetFilePos(file))(file,&file_offset) == EOF ) {
      return (*theIFileLastError(file))(file);
    }

#ifdef IC_NO_RSD /* WARNING! unbuildable, untested code */
  } else {

    /* This is a rewindable but not seekable file */
    int32 c;

    /* first rewind, if we need to */
    if ( offset < file->file_position ) {
      /* rewind, and exit early if we were seeking to the start */
      if (*theIMyResetFile(file))(file) == EOF)
        return (*theIFileLastError(file))(file);
      if ( offset == 0 )
        return TRUE;
    }

    /* discard buffers until we are in the right area */
    while ( file->count == 0 ||
            file->file_position + file->buffersize < offset ) {
      c = GetNextBuf(file));
      if (c != EOF) {
        *(--file->ptr) = c;
        ++file->count;
      }
    }

    /* seek is now within the current buffer */
    offset -= file->file_position;
    file->ptr += offset;
    file->count -= offset;

  }
#endif /* end of warning */

  return TRUE;
}


/*----------------------------------------------------------------------------*/
/* Following adapted from icread.c */

static Bool readHeader( FILELIST* flptr, icHeader *icc_header )
{
  /* Go read the header */
  if ( !mi_seek(flptr, 0) ||
       file_read(flptr, (uint8 *)icc_header, sizeof(icHeader), NULL) <= 0 )
    return detail_error_handler( IOERROR, "Unable to read ICC profile header." );

  /* fix the byte order in the header */
  iccfix_header(icc_header);

  /* are we an ICC file? */
  if (icc_header->magic != icMagicNumber)
    return detail_error_handler( SYNTAXERROR,
            "ICC profile header has invalid profile file signature." );

  return TRUE;
}

static Bool readTags( FILELIST *flptr, int32 *number_tags, icTags **taglist )
{
  int32   i;
  int32   num_tags;
  icTags  *tags;

  *number_tags = 0;
  *taglist = NULL;

  /* seek to the start of the tags (immediately after the header) */
  if (!mi_seek(flptr, sizeof(icHeader)))
    return detail_error_handler( IOERROR,
                                 "Unable to find ICC profile tag table." );

  /* read the tag count */
  if ( file_read(flptr, (uint8 *) &num_tags, sizeof(int32), NULL) <= 0 )
    return detail_error_handler( IOERROR,
                                 "Unable to read ICC profile tag count." );

  icfixu32(num_tags);

  /* allocate space to hold the tags */
  tags = mi_alloc(num_tags * sizeof(icTags));
  if (tags == NULL)
    return FALSE;

  for (i = 0; i < num_tags; i++) {
    if ( file_read(flptr, (uint8 *)&tags[i].tag, sizeof(icTag), NULL) <= 0 ) {
      (void) detail_error_handler( IOERROR,
                                   "Unable to read ICC profile tags." );
      goto tidyup;
    }

    /* fix the tag */
    iccfix_tag(&tags[i].tag);
  }

  /* go back and fetch the points_to */
  for ( i = 0; i < num_tags; i++ ) {
    if (!mi_seek(flptr, tags[i].tag.offset)) {
      (void) detail_error_handler( IOERROR,
                                   "Unable to find tag data in ICC profile." );
      goto tidyup;
    }

    /* read the signature */
    if ( file_read(flptr, (uint8 *)&(tags[i].points_at), sizeof(uint32), NULL) <= 0 ) {
      (void) detail_error_handler( IOERROR,
                                   "Unable to read ICC profile tag type signature." );
      goto tidyup;
    }

    /* fix the points_at value */
    icfixsig(tags[i].points_at);
  }

  *number_tags = num_tags;
  *taglist = tags;

  return TRUE;

tidyup:
  mi_free(tags);
  return FALSE;
}

/* only use ZERO for initialising structs comprising ints and bytes */
#define ZERO(_data,_size) HqMemZero((_data),(_size))
#define ZEROINIT(_data,_type) \
{ _type zeroinit = {0}; \
  *(_data) = zeroinit; \
}

static Bool read_XYZPoint(FILELIST       *flptr,
                          int32          num_tags,
                          icTags*        tags,
                          icTagSignature point_sig,
                          icXYZType      *xyz)
{
  icTags  *p;

  /* the tag has to be here! */
  if ((p = findTag(point_sig, num_tags, tags)) == NULL)
    return detail_error_handler( UNDEFINED,
            "ICC profile tag table entry for XYZType tag not found." );

  /* the tag has to be at least the size of the struct */
  if (p->tag.size < sizeof(*xyz))
    return detail_error_handler( SYNTAXERROR,
            "XYZType tag size too small in ICC profile tag table." );

  /* Seek to the tag */
  if (!mi_seek(flptr, p->tag.offset))
    return detail_error_handler( IOERROR,
            "XYZType tag data not found in ICC profile." );

  /* read in the tag */
  if ( file_read(flptr, (uint8 *)xyz, sizeof(*xyz), NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read XYZType tag data in ICC profile." );

  icfixsig(xyz->base.sig);

  if (xyz->base.sig != icSigXYZType) {
    ZERO(xyz, sizeof(icXYZType));
    return detail_error_handler( SYNTAXERROR,
            "XYZType tag has incorrect type signature in ICC profile." );
  }

  /* fix the byte order of the data */
  iccfix_xyz(&(xyz->data.data[0]));

  return TRUE;
}

static Bool read_lut_heading(icTags                **p,
                             FILELIST              *flptr,
                             int32                 num_tags,
                             icTags                *tags,
                             icTagSignature        desc_sig,
                             LUT_TEMP_DATA         *temp,
                             LUT_16_TEMP_DATA      *temp_16lut)
{
  /* the tag has to be here! */
  if (((*p) = findTag(desc_sig, num_tags, tags)) == NULL)
    return detail_error_handler( UNDEFINED,
            "ICC profile tag table entry for lut8Type or lut16Type tag not found." );

  /* the tag has to be at least the size of the struct */
  if ((*p)->tag.size < sizeof(temp))
    return detail_error_handler( SYNTAXERROR,
            "lut8Type or lut16Type tag size too small in ICC profile tag table." );

  /* Seek to the tag */
  if (!mi_seek(flptr, (*p)->tag.offset))
    return detail_error_handler( IOERROR,
            "lut8Type or lut16Type tag data not found in ICC profile." );

  if ( file_read(flptr, (uint8 *)temp, sizeof(*temp), NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read lut8Type or lut16Type tag data in ICC profile" );

  /* if the tag doesn't match the 8/16 bit LutType then we have a problem */
  icfixsig(temp->sig);

  if (temp->sig != icSigLut16Type && temp->sig != icSigLut8Type )
    return detail_error_handler( SYNTAXERROR,
            "lut8Type or lut16Type tag has incorrect type signature in ICC profile." );

  icfixs15f16(temp->e00);
  icfixs15f16(temp->e01);
  icfixs15f16(temp->e02);
  icfixs15f16(temp->e10);
  icfixs15f16(temp->e11);
  icfixs15f16(temp->e12);
  icfixs15f16(temp->e20);
  icfixs15f16(temp->e21);
  icfixs15f16(temp->e22);

  /* if its is a Lut16, read the number of entries and fix them else set
   * the size to be constant */
  if (temp->sig == icSigLut16Type ) {
    if ( file_read(flptr, (uint8 *)temp_16lut, sizeof(*temp_16lut), NULL) <= 0 )
      return detail_error_handler( IOERROR,
              "Unable to read ICC profile lut16Type number of input or output table entries." );
    icfixu16(temp_16lut->inputEnt);
    icfixu16(temp_16lut->outputEnt);
  } else {
    temp_16lut->inputEnt = 256;
    temp_16lut->outputEnt = 256;
  }

  return TRUE;
}

static Bool read_ab_ba_lut_heading(icTags                **p,
                                   FILELIST              *flptr,
                                   int32                 num_tags,
                                   icTags                *tags,
                                   icTagSignature        desc_sig,
                                   LUT_AB_BA_TEMP_DATA   *temp)
{
  /* the tag has to be here! */
  if (((*p) = findTag(desc_sig, num_tags, tags)) == NULL)
    return detail_error_handler( UNDEFINED,
            "ICC profile tag table entry for lutAtoBType or lutBtoAType tag not found." );

  /* the tag has to be at least the size of the struct */
  if ((*p)->tag.size < sizeof(temp))
    return detail_error_handler( SYNTAXERROR,
            "lutAtoBType or lutBtoAType tag size too small in ICC profile tag table." );

  /* Seek to the tag */
  if (!mi_seek(flptr, (*p)->tag.offset))
    return detail_error_handler( IOERROR,
            "lutAtoBType or lutBtoAType tag data not found in ICC profile.");

  /* read in the tag */
  if ( file_read(flptr, (uint8 *)temp, sizeof(*temp), NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read lutAtoBType or lutBtoAType tag data in ICC profile." );

  /* if the tag doesn't match the AtoB or BtoA LutType then we have a problem */
  icfixsig(temp->sig);

  if (temp->sig != icSiglutAtoBType &&
      temp->sig != icSiglutBtoAType)
    return detail_error_handler( SYNTAXERROR,
            "lutAtoBType or lutBtoAType tag has incorrect type signature in ICC profile." );

  /* fix the entries to suit the machine type */
  icfixu32(temp->offsetBcurve);
  icfixu32(temp->offsetMatrix);
  icfixu32(temp->offsetMcurve);
  icfixu32(temp->offsetCLUT);
  icfixu32(temp->offsetAcurve);

  return TRUE;
}


static Bool read_matrix(FILELIST *flptr, uint32 offset, SYSTEMVALUE matrix[3][4] )
{
  int32 i, j;
  int32 temp[12];

  if (!mi_seek(flptr, offset))
    return detail_error_handler( IOERROR,
            "Unable to find lutAtoBType or lutBtoAType matrix data in ICC profile." );

  if ( file_read(flptr, (uint8 *)temp, 12*sizeof(int32), NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read lutAtoBType or lutBtoAType matrix data in ICC profile." );

  /* Fix up all the entries and convert to correct units */
  for ( i=0; i<3; i++) {
    for ( j=0; j<3; j++ ) {
      icfixs15f16(temp[i*3+j]);
      matrix[i][j] = temp[i*3+j] / 65536.0f;
    }
    icfixs15f16(temp[i+9]);
    matrix[i][3] = temp[i+9] / 65536.0f;
  }

  return TRUE;
}


static Bool read_curve_type( FILELIST *flptr,
                             icUInt32Number offset,
                             icTagTypeSignature *curve_sig )
{
#define curve_sig_size (4)       /* signature */

  if (!mi_seek(flptr, offset))
    return detail_error_handler( IOERROR,
            "Unable to find curve data in ICC profile." );

  if ( file_read(flptr, (uint8 *)curve_sig, curve_sig_size, NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read curve signature in ICC profile." );

  icfixsig(*curve_sig);

  return TRUE;
}


static Bool read_trc_curve_type( FILELIST *flptr,
                                 icTagSignature text_sig,
                                 int32 num_tags,
                                 icTags* tags,
                                 icTagTypeSignature *curve_sig,
                                 icUInt32Number *offset )
{
  icTags *p;

  if ((p = findTag(text_sig, num_tags, tags)) == NULL)
    return detail_error_handler( UNDEFINED,
            "ICC profile tag table entry for TRC tag not found." );

  if (! read_curve_type( flptr, p->tag.offset, curve_sig ))
    return FALSE;

  *offset = p->tag.offset;

  return TRUE;
}

static Bool get_para_info( FILELIST* flptr,
                           icUInt32Number offset,
                           int32 *para_length,
                           int32 *para_type )
{
  /* The lengths of parametricCurveType function types 0 through 4 */
  int32 len[] = {1,3,4,5,7};

  icParametricCurveFullType temp;

  ZERO(&temp, sizeof(icParametricCurveFullType));

  if (!mi_seek(flptr, offset))
    return detail_error_handler( IOERROR,
            "Unable to find parametricCurveType data in ICC profile." );

  if ( file_read(flptr, (uint8 *)&temp, curv_header_size, NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read parametricCurveType data in ICC profile." );

  icfixsig(temp.base.sig);
  if (temp.base.sig != icSigParametricCurveType)
    return detail_error_handler( UNDEFINED,
            "parametricCurveType has incorrect type signature in ICC profile." );

  icfixu16(temp.params.funcType);

  if (temp.params.funcType > 4 )
    return detail_error_handler( SYNTAXERROR,
            "parametricCurveType has invalid function type in ICC profile." );

  *para_type = (int32) temp.params.funcType;
  *para_length = len[ (int32) temp.params.funcType ];

   return TRUE;
}


static Bool read_para_data( FILELIST *flptr,
                            uint32 offset,
                            uint32 length,
                            int32 curve_type,
                            DATA_PARAMETRIC *curve)
{
  int32                     bytes_to_read;
  int32                     type;
  icParametricCurveFull     temp;
  uint16                    gamma = 0;

  HQASSERT( flptr, "Null flptr in read_para_data" );
  HQASSERT( curve, "Null curve in read_para_data" );
  HQASSERT( curve_type != -1 || length == 1,
            "Inconsistent curve type in read_para_data" );

  type = curve_type;

  ZEROINIT(curve,DATA_PARAMETRIC);
  ZERO(&temp, sizeof(icParametricCurveFull));

  if (!mi_seek(flptr, offset))
    return detail_error_handler( IOERROR,
            "Unable to find curve data in ICC profile." );

  /* A curve_type of -1 indicates that the gamma value was actually stored
     in a curveType rather than a parametricCurveType in the icc profile */
  if ( curve_type == -1 ) {
    if ( file_read(flptr, (uint8 *) &gamma, sizeof(uint16), NULL) <= 0 )
      return detail_error_handler( IOERROR,
              "Unable to read gamma from curveType in ICC profile." );

    /* Fix the value and correct its units */
    icfixu16(gamma);
    curve->gamma = (USERVALUE) gamma / 256.0f;
    type = 0;
  } else {
    /* It was a parametricCurveType */
    bytes_to_read = length * sizeof(int32);

    if ( file_read(flptr, (uint8 *)&temp.gamma, bytes_to_read, NULL) <= 0 )
      return detail_error_handler( IOERROR,
              "Unable to read parametricCurveType data in ICC profile." );

    /* Fix the values and correct their units */
    icfixs15f16(temp.gamma);
    icfixs15f16(temp.a);
    icfixs15f16(temp.b);
    icfixs15f16(temp.c);
    icfixs15f16(temp.d);
    icfixs15f16(temp.e);
    icfixs15f16(temp.f);

    curve->gamma = temp.gamma / 65536.0f;
    curve->a = (temp.a) /65536.0f;
    curve->b = (temp.b) /65536.0f;
    curve->c = (temp.c) /65536.0f;
    curve->d = (temp.d) /65536.0f;
    curve->e = (temp.e) /65536.0f;
    curve->f = (temp.f) /65536.0f;
  }

  /* Adjust the params if necessary to store them all as type 4 curves */
  if (type == 0) {
    curve->a = 1.0f;
  } else if (type == 1) {
    curve->d = - curve->b / curve->a;
  } else if (type == 2 ) {
    curve->d = - curve->b / curve->a;
    curve->e = curve->f = curve->c;
    curve->c = 0.0f;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* The inverse parametric is complicated by there being no requirement in the
 * ICC spec for the linear and gamma functions to be monotonic or continuous.
 * This means there are a large number of pathological cases which could be met.
 *
 * Here we limit the domains of the two inverse functions to the range of Y they
 * produce uninverted. If there is a gap between these two domains, we linearly
 * interpolate to produce a smoother response than thresholding would. Y values
 * outside the range of these domains are clipped to nearest.
 *
 * This is achieved by calculating these domains, the values to be used
 * outside them, the equation of the additional interpolation if required, and
 * inverting certain parameters to avoid division in the mini-invoke.
 */
static Bool read_inverse_para_data( FILELIST *flptr,
                                    uint32 offset,
                                    uint32 length,
                                    int32 curve_type,
                                    DATA_INVERSE_PARAMETRIC *curve)
{
  USERVALUE p,q,r,s;
  enum { IGNORE = 100 };   /* unfeasibly large value for domain comparisons */

  /* Read in the original parameters, uninverted */
  if ( !read_para_data(flptr, offset, length, curve_type, &curve->p) )
    return FALSE;

  /* Get Y domains of linear and gamma functions, linear=[p,q], gamma=[r,s] */
  p = curve->p.f;
  q = curve->p.f + curve->p.c * min(1,curve->p.d);
  r = curve->p.e + (USERVALUE)pow(curve->p.a * max(0,curve->p.d) + curve->p.b,
                                  curve->p.gamma);
  s = curve->p.e + (USERVALUE)pow(curve->p.a + curve->p.b , curve->p.gamma);

  /* Domain of the linear segment */
  curve->lin_min = min(p,q);
  curve->lin_max = max(p,q);

  /* Domain of the gamma segment */
  curve->crv_min = min(r,s);
  curve->crv_max = max(r,s);

  /* Assume no gap */
  curve->gap_min = IGNORE;
  curve->gap_max = IGNORE;

  /* spot one domain entirely subsumed by the other */
  if ( curve->lin_min >= curve->crv_min && curve->lin_max <= curve->crv_max ) {
    /* Linear section subsumed */

    curve->above = (curve->p.a < 0) ? curve->p.d : 1.0f;
    curve->below = (curve->p.a < 0) ? 1.0f : curve->p.d;

    curve->lin_min = IGNORE;
    curve->lin_max = IGNORE;

  } else if ( curve->crv_min >= curve->lin_min &&
              curve->crv_max <= curve->lin_max ) {
    /* Gamma section subsumed */

    curve->above = (curve->p.c < 0) ? 0.0f : curve->p.d;
    curve->below = (curve->p.c < 0) ? curve->p.d : 0.0f;

    curve->crv_min = IGNORE;
    curve->crv_max = IGNORE;

  } else if ( curve->crv_min < IGNORE && curve->lin_max < IGNORE &&
              curve->crv_min > curve->lin_max ) {
    /* Gap between segments, linear below - interpolate */

    curve->above = (curve->p.a < 0) ? curve->p.d : 1.0f;
    curve->below = (curve->p.c < 0) ? curve->p.d : 0.0f;

    curve->gap_min = curve->lin_max;
    curve->gap_max = curve->crv_min;

    p = ( curve->p.c < 0 ) ? 0.0f : curve->p.d;   /* X at Y=gap_min */
    q = ( curve->p.a < 0 ) ? curve->p.d : 1.0f;   /* X at Y=gap_max */

  } else if ( curve->lin_min < IGNORE && curve->crv_max < IGNORE &&
              curve->lin_min > curve->crv_max ) {
    /* Gap between segments, linear above - interpolate */

    curve->above = (curve->p.c < 0) ? 0.0f : curve->p.d;
    curve->below = (curve->p.a < 0) ? 1.0f : curve->p.d;

    curve->gap_min = curve->crv_max;
    curve->gap_max = curve->lin_min;

    p = ( curve->p.c < 0 ) ? curve->p.d : 0.0f;   /* X at Y=gap_min */
    q = ( curve->p.a < 0 ) ? 1.0f : curve->p.d;   /* X at Y=gap_max */

  } else if ( curve->lin_min < curve->crv_min &&
              curve->lin_max > curve->crv_min ) {
    /* overlap, with linear below. Interpolate the overlap */

    curve->above = (curve->p.a < 0) ? curve->p.d : 1.0f;
    curve->below = (curve->p.c < 0) ? curve->p.d : 0.0f;

    curve->gap_min = curve->crv_min;
    curve->gap_max = curve->lin_max;
    curve->crv_min = curve->gap_max;
    curve->lin_max = curve->gap_min;

    p = (curve->gap_min - curve->p.f) / curve->p.c;
    q = ( (USERVALUE)pow(curve->gap_max - curve->p.e , 1 / curve->p.gamma) -
          curve->p.b ) / curve->p.a;

  } else if ( curve->crv_min < curve->lin_min &&
              curve->crv_max > curve->lin_min ) {
    /* Overlap, with linear above. Interpolate the overlap */

    curve->above = (curve->p.c < 0) ? 0.0f : curve->p.d;
    curve->below = (curve->p.a < 0) ? 1.0f : curve->p.d;

    curve->gap_min = curve->lin_min;
    curve->gap_max = curve->crv_max;
    curve->lin_min = curve->gap_max;
    curve->crv_max = curve->gap_min;

  }

  if ( curve->gap_min < IGNORE ) {
    /* there is a gap, so calculate the interpolation equation */
    curve->gradient = (q - p) / (curve->gap_max - curve->gap_min);
    curve->offset = p - curve->gradient * curve->gap_min;
  }

  /* remove non-invertible (horizontal) segments */
  if ( curve->lin_min == curve->lin_max ) {
    /* linear segment is horizontal, c must be zero */
    curve->lin_min = IGNORE;
    curve->lin_max = IGNORE;
  }
  if ( curve->crv_min == curve->crv_max ) {
    /* gamma segment is horizontal, gamma and/or a must be zero */
    curve->crv_min = IGNORE;
    curve->crv_max = IGNORE;
  }

  /* find the 'below' threshold, and sanity check */
  curve->minimum = min(curve->crv_min, curve->crv_max);
  HQASSERT( curve->minimum < IGNORE, "Failed to construct inverse parametric");

  /* Invert values now so the mini-invoke can multiply instead of divide.
   * Zero values are not used anyway, as their domains are removed above. */
  if ( curve->p.a != 0 )     curve->p.a     = 1 / curve->p.a;
  if ( curve->p.c != 0 )     curve->p.c     = 1 / curve->p.c;
  if ( curve->p.gamma != 0 ) curve->p.gamma = 1 / curve->p.gamma;

  return TRUE;
}

/*----------------------------------------------------------------------------*/

static Bool get_curv_length( FILELIST*      flptr,
                             icUInt32Number offset,
                             int32          *curve_length )
{
  icCurveType temp;

  if (!mi_seek(flptr, offset))
    return detail_error_handler( IOERROR,
            "Unable to find curveType data in ICC profile." );

  if ( file_read(flptr, (uint8 *)&temp, curv_header_size, NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read curveType data in ICC profile." );

  icfixsig(temp.base.sig);
  if (temp.base.sig != icSigCurveType)
    return detail_error_handler( UNDEFINED,
            "curveType has incorrect curve signature in ICC profile." );

  icfixu32(temp.curve.count);

  *curve_length = temp.curve.count;

  return TRUE;
}

static Bool read_curv_data( Bool                  monotonise,
                            FILELIST              *flptr,
                            uint32                offset,
                            uint32                entries,
                            DATA_PIECEWISE_LINEAR *curve )
{
  int32           bytes_to_read;
  uint32          i;
  uint16*         temp;
  Bool            inverse;

  HQASSERT( flptr, "Null file pointer in read_curv_data" );
  HQASSERT( offset > sizeof(icHeader), "Offset too small in read_curv_data" );
  HQASSERT( entries > 0, "Curve length too small in read_curv_data" );
  HQASSERT( curve, "Null curve in read_curv_data" );

  /* Seek to the curve data */
  if (!mi_seek( flptr, offset ))
    return detail_error_handler( IOERROR,
            "1D curve or table data not found in ICC profile." );

  bytes_to_read = entries * sizeof(uint16);
  /* Normally one would load the data at values[0], and then loop backwards
   * from the last entry to avoid overwriting any. However, we need to loop
   * forwards to force monotonicty, so load the temp array at the END of the
   * values[] array.
   */
  temp = ((uint16*) &(curve->values[entries])) - entries;

  if ( file_read(flptr, (uint8 *)temp, bytes_to_read, NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read 1D curve or table data in ICC profile." );

  inverse = (temp[entries-1] < temp[0]);  /* does curve ascend or descend? */

  for (i = 0; i < entries; i++) {
    icfixu16(temp[i]);
    if ( monotonise &&
         i > 0 &&
         (temp[i] < temp[i-1]) != inverse )
      curve->values[i] = curve->values[i-1];  /* flatten local inversion */
    else
      curve->values[i] = (temp[i]) / 65535.0f;
  }

  return TRUE;
}

static Bool read_8bit_curv_data( FILELIST              *flptr,
                                 uint32                offset,
                                 uint32                entries,
                                 DATA_PIECEWISE_LINEAR *curve )
{
  int32           bytes_to_read;
  int32           i;
  uint8*          temp;

  HQASSERT( flptr, "Null file pointer in read_8bit_curv_data" );
  HQASSERT( offset > sizeof(icHeader),
            "Offset too small in read_8bit_curv_data" );
  HQASSERT( entries > 0, "Curve length too small in read_8bit_curv_data" );
  HQASSERT( curve, "Null curve in read_8bit_curv_data" );

  /* Seek to the curve data */
  if (!mi_seek( flptr, offset ))
    return detail_error_handler( IOERROR,
            "Unable to find 8-bit 1D table data in ICC profile." );

  bytes_to_read = entries * sizeof(uint8);
  temp = (uint8*) &(curve->values[0]);

  if ( file_read(flptr, (uint8 *)temp, bytes_to_read, NULL) <= 0 )
    return detail_error_handler( IOERROR,
            "Unable to read 8-bit 1D table data in ICC profile." );

  for (i = entries - 1; i >= 0; i--) {
    /* Do this backwards so we don't overwrite our values */
    curve->values[i] = temp[i] / 255.0f;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Following adapted from iconvert.c */

void note_interesting_tags( int32 num_tags, icTags *tags, int32 *tagsfound )
{
  /*
   * Work through the list of tags and note the presence of interesting ones
   */
  int32           i;
  int32           j;
  icTagSignature  try_this_tag;

  HQASSERT( num_tags, "Null num_tags in note_interesting_tags" );

  for (i = 0; i < num_tags; i++) {
    for (j = 0;
         (try_this_tag = Interesting_Tags[j]) != 0;
         j++)  {
      if (tags[i].tag.sig == (icSignature)try_this_tag) {
        *tagsfound |= 1 << j;
        break;
      }
    }
  }
}

/*----------------------------------------------------------------------------*/
/* Following adapted from icolor.c */

static uint32 GetMajorVersion(icUInt32Number  Version)
{
  return ( (Version & 0xFF000000) >> 24 );
}

static Bool ICSupportVersion( icHeader* icc_header )
{
  /* We support all 3.0 ICC profiles and above, these have major version 2
   * and minor version 0.
   */
  uint32  nHeaderVersion = GetMajorVersion( icc_header->version );
  uint32  nSupportedVersion = 2;

  return ( nHeaderVersion >= nSupportedVersion );
}

static Bool isNcolorspace( icColorSpaceSignature space )
{
  return ( ( space & icNCLRmask ) == icNCLRkey );
}

/* If we can handle the colorspace convert it to our internal format */
static Bool convert_colorspace(icColorSpaceSignature sig_space,
                               icProfileClassSignature deviceClass,
                               COLORSPACE_ID *space)
{
  *space = SPACE_notset;

  if (isNcolorspace(sig_space)) {
    *space = SPACE_DeviceN;
    return TRUE;
  } else {
    switch (sig_space) {
    case icSigCmykData:
      *space = SPACE_DeviceCMYK;
      return TRUE;

    case icSigCmyData:
      *space = SPACE_DeviceCMY;
      return TRUE;

    case icSigRgbData:
      *space = SPACE_DeviceRGB;
      return TRUE;

    case icSigGrayData:
      *space = SPACE_DeviceGray;
      return TRUE;

    case icSigLabData:
      if (deviceClass == icSigColorSpaceClass) {
        *space = SPACE_Lab;
        return TRUE;
      } else {
        return detail_error_handler( CONFIGURATIONERROR,
                                     "Unable to handle colorspace in ICC profile." );
      }

    case icSigXYZData:
    case icSigLuvData:
    case icSigYCbCrData:
    case icSigYxyData:
    case icSigHsvData:
    case icSigHlsData:
      return detail_error_handler( CONFIGURATIONERROR,
                                   "Unable to handle colorspace in ICC profile." );

    default:
      return detail_error_handler( SYNTAXERROR,
                                   "Invalid colorspace in ICC profile." );
    }
  }
}

/* If we can handle the pcs space convert it to our internal format */
static Bool convert_pcs_space(icHeader *icc_header, COLORSPACE_ID *space)
{
  *space = SPACE_notset;

  if (icc_header->deviceClass == icSigLinkClass) {
    switch (icc_header->pcs) {
      case icSigXYZData:
      case icSigLabData:
        return detail_error_handler( SYNTAXERROR,
                "Invalid PCS space for devicelink ICC profile." );

      default:
        return convert_colorspace( icc_header->pcs,
                                   icc_header->deviceClass,
                                   space );
    }
  } else {
    switch (icc_header->pcs) {
      case icSigXYZData:
        *space = SPACE_ICCXYZ;
        return TRUE;

      case icSigLabData:
        *space = SPACE_ICCLab;
        return TRUE;

      default:
        return detail_error_handler( SYNTAXERROR,
                "Invalid PCS space for ICC profile." );
    }
  }
}

static int8 getNumNColors( icColorSpaceSignature space )
{
  int8 numColors;

  HQASSERT( isNcolorspace( space ), "expected nCLR space in getNumNColors" );

  /* the value of n is given by the first two bytes of the space.  */
  numColors = ( int8 ) ( ( space - icSig0colorData ) >> 24 );

  /* Check the range is OK */
  if ( ( 2 <= numColors ) && ( numColors <= 15 ) )
    return numColors;

  /* numColors is out of range */
  return -1;
}

/* Return the number of input channels the colorSpace contains
 * or -1 if space is not recognized
 */
static int8 number_colors(icColorSpaceSignature space)
{
  int8 numColors = -1;

  /* First see if it is one of the spaces other than N color */
  switch (space) {
  case icSigGrayData:
    return 1;

  case icSigXYZData:
  case icSigLabData:
  case icSigLuvData:
  case icSigYCbCrData:
  case icSigYxyData:
  case icSigRgbData:
  case icSigHsvData:
  case icSigHlsData:
  case icSigCmyData:
    return 3;

  case icSigCmykData:
    return 4;
  }

  /* See if it is an 'nCLR' space.  If it is, find the value of 'n' */
  if ( isNcolorspace( space ) ) {
    numColors = getNumNColors( space ) ;
    return numColors;
  }

  HQFAIL("Unrecognised colorspace in number_colors");
  (void) detail_error_handler( SYNTAXERROR,
          "Invalid colorspace in ICC profile." );
  return -1;
}


/* Fill in the relative white and black points in the profile info cache */
static Bool get_rel_white_black_info( ICC_PROFILE_INFO *pInfo,
                                      int32            num_tags,
                                      icTags           *tags )
{
  icXYZType           XYZ_temp;
  FILELIST*           flptr;

  HQASSERT( pInfo, "Null pInfo in get_rel_white_black_info" );
  HQASSERT( pInfo->filelist_head,
            "Null filelist head in get_rel_white_black_info" );
  HQASSERT( tags, "Null tags in get_rel_white_black_info" );

  flptr = pInfo->filelist_head->file;

  HQASSERT( flptr, "Null file pointer in get_rel_white_black_info" );

  if (findTag(icSigMediaWhitePointTag, num_tags, tags) != NULL) {
    if ( !read_XYZPoint(flptr, num_tags, tags,
                        icSigMediaWhitePointTag, &XYZ_temp))
      return FALSE;

    if ((XYZ_temp.data.data[0].X != 0) &&
        (XYZ_temp.data.data[0].Y != 0) &&
        (XYZ_temp.data.data[0].Z != 0)) {
      pInfo->relative_whitepoint[0] = XYZ_temp.data.data[0].X / 65536.0f;
      pInfo->relative_whitepoint[1] = XYZ_temp.data.data[0].Y / 65536.0f;
      pInfo->relative_whitepoint[2] = XYZ_temp.data.data[0].Z / 65536.0f;
    } else {
      /* The media white point is duff, it contains a zero value. */
      monitorf(UVS("Warning: Media white point value is invalid - using illuminant instead\n"));
    }
  } else {
    /* should be a device link */
    if (pInfo->deviceClass != icSigLinkClass) {
      /* There is a bug in Quark profiles such that the media white
       * point exists, but is the wrong tag type.
       */
      monitorf(UVS("Warning: Media white point tag not found - using illuminant instead\n"));
    }
  }


  /* The relative blackpoint is optional */
  if (findTag(icSigMediaBlackPointTag, num_tags, tags) != NULL) {
    if ( !read_XYZPoint(flptr, num_tags, tags,
                        icSigMediaBlackPointTag, &XYZ_temp))
      return FALSE;

    pInfo->relative_blackpoint[0] = XYZ_temp.data.data[0].X / 65536.0f;
    pInfo->relative_blackpoint[1] = XYZ_temp.data.data[0].Y / 65536.0f;
    pInfo->relative_blackpoint[2] = XYZ_temp.data.data[0].Z / 65536.0f;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/

#ifdef ASSERT_BUILD
void iccbasedInfoAssertions(CLINKiccbased *pIccBasedInfo)
{
  HQASSERT(pIccBasedInfo != NULL, "pIccBasedInfo not set");
  HQASSERT(pIccBasedInfo->profile != NULL, "null icc profile info cache");
  HQASSERT(pIccBasedInfo->intent < N_ICC_TABLES, "Invalid ICC table");
  HQASSERT(pIccBasedInfo->i_dimensions > 0 &&
           pIccBasedInfo->i_dimensions <= MAX_CHANNELS,
           "Invalid number of input dimensions" );
  HQASSERT(pIccBasedInfo->o_dimensions > 0 &&
           pIccBasedInfo->o_dimensions <= MAX_CHANNELS,
           "Invalid number of output dimensions" );
}
#endif


static void iccbased_free(CLINKiccbased *link)
{
  DATA_ACTION * action;

  /* discard the mini-invoke data */
  for (action = &link->actions[0]; action->function; action++) {
    /* discard the extra allocations for some actions */
    if (action->function == mi_clut8 && action->u.clut8 != NULL)
      mi_free(action->u.clut8->scratch);
    else if (action->function == mi_clut16 && action->u.clut16 != NULL)
      mi_free(action->u.clut16->scratch);

    mi_free(action->u.data);
  }

  mi_free(link);
}


/* ACTUALLY destroy an iccbased CLINKiccbased */
static void iccbased_discard(CLINKiccbased *pICC)
{
  int i;
  ICC_PROFILE_INFO *profile;

  HQASSERT(pICC, "Null link pointer in iccbased_discard");
  iccbasedInfoAssertions(pICC);

  profile = pICC->profile;

  for (i=0; i<N_ICC_TABLES; i++) {
    if (profile->dev2pcs[i] == pICC)
      CLINK_RELEASE(&profile->dev2pcs[i], iccbased_free);

    if (profile->pcs2dev[i] == pICC)
      CLINK_RELEASE(&profile->pcs2dev[i], iccbased_free);
  }
}


/* As they're cached, we don't really destroy CLINKiccbased links here */
static void iccbased_destroy(CLINK *pLink)
{
  CLINK_RELEASE(&pLink->p.iccbased, iccbased_free);
  cc_common_destroy(pLink);
}

static void profile_free(ICC_PROFILE_INFO *profile)
{
  int i;
  CLINKiccbased *icc;

  HQASSERT(profile->filelist_head == NULL, "ICC filelist != NULL, memory leak");

  /* discard the links */
  for (i=0; i < N_ICC_TABLES; i++) {
    if ((icc=profile->dev2pcs[i]) != NULL)
      iccbased_discard(icc);
    if ((icc=profile->pcs2dev[i]) != NULL)
      iccbased_discard(icc);
  }

  /* discard colorant tables */
  mi_free(profile->device_colorants);
  mi_free(profile->pcs_colorants);

  /* discard the profile cache */
  mi_free(profile);
}

static void profile_discard(COLOR_STATE       *colorState,
                            ICC_PROFILE_INFO  *profile,
                            Bool              detach)
{
  HQASSERT(colorState != NULL, "colorState NULL");
  HQASSERT(profile != NULL, "profile NULL");

  /* detach from the ICC cache list, if necessary */
  if (detach) {
    ICC_PROFILE_INFO_CACHE *cacheItem, *previous = NULL;
    for (cacheItem = colorState->ICC_cacheHead;
         cacheItem != NULL;
         cacheItem = cacheItem->next) {
      if (cacheItem->d == profile) {
        if (previous)
          previous->next = cacheItem->next;
        else
          colorState->ICC_cacheHead = cacheItem->next;
        break;
      }
      previous = cacheItem;
    }

    /* Free the list entry, but not yet the profile */
    HQASSERT(cacheItem != NULL, "ICC profile not found");
    mi_free(cacheItem);
  }

  /* discard any ICC_FILELIST_INFO structures.
   * NB. These are always front end only, so they must be discarded when
   * discarding from the front end regardless of whether the rest of the profile
   * will be discarded.
   */
  if (colorState == frontEndColorState) {
    ICC_FILELIST_INFO *listinfo, *nextinfo = profile->filelist_head;

    while (nextinfo != NULL) {
      listinfo = nextinfo;
      nextinfo = listinfo->next;
      mi_free(listinfo);
    }
    profile->filelist_head = NULL;
  }

  CLINK_RELEASE(&profile, profile_free);
}

/*----------------------------------------------------------------------------*/

static void initialise_xyzinfo( icXYZNumber illuminant,
                                ICC_PROFILE_INFO *pInfo )
{
  int i;
  XYZVALUE D50 = {0.9642f, 1.0f, 0.8249f};
  XYZVALUE D65 = {0.9504f, 1.0f, 1.0889f};

  HQASSERT( pInfo, "Null pInfo in initialise_xyzinfo" );

  /* Initialise the whitepoint and relative whitepoint from the illuminant
   * The relative whitepoint will later be overwritten by info from the
   * mediaWhitePointTag if present.
   * N.B. The blackpoint and relative blackpoint are already correctly
   * intialised to zeros.
   */
  pInfo->whitepoint[0] = illuminant.X / 65536.0f;
  pInfo->whitepoint[1] = illuminant.Y / 65536.0f;
  pInfo->whitepoint[2] = illuminant.Z / 65536.0f;

  if (( fabs(pInfo->whitepoint[0] - D50[0]) < EPSILON ) &&
      ( fabs(pInfo->whitepoint[1] - D50[1]) < EPSILON ) &&
      ( fabs(pInfo->whitepoint[2] - D50[2]) < EPSILON )) {
    /* Set it to D50 */
    for (i=0;i<3;i++)
      pInfo->whitepoint[i] = D50[i];
  }
  else if (( fabs(pInfo->whitepoint[0] - D65[0]) < EPSILON ) &&
           ( fabs(pInfo->whitepoint[1] - D65[1]) < EPSILON ) &&
           ( fabs(pInfo->whitepoint[2] - D65[2]) < EPSILON )) {
    /* Set it to D65 */
    for (i=0;i<3;i++)
      pInfo->whitepoint[i] = D65[i];
    monitorf(UVS( "Warning: D65 illuminant used in ICC profile.\n"));
  }
  else if (( pInfo->whitepoint[0] < EPSILON ) ||
           ( pInfo->whitepoint[1] < EPSILON ) ||
           ( pInfo->whitepoint[2] < EPSILON )) {
    monitorf(UVS( "Warning: Invalid ICC profile illuminant - used D50 instead.\n"));

    /* Set it to D50 */
    for (i=0;i<3;i++)
      pInfo->whitepoint[i] = D50[i];
  }
  else
    monitorf(UVS( "Warning: Unknown illuminant used in ICC profile.\n"));

  /* N.B. may not want to do this in the case of a device link */
  for (i=0;i<3;i++)
    pInfo->relative_whitepoint[i] = pInfo->whitepoint[i];
}


/* Fill in the pInfo structure with information from the icc profile header */
static Bool query_profile_header( ICC_PROFILE_INFO *pInfo )
{
  icHeader icc_header = {0};

  HQASSERT( pInfo, "Null pInfo in query_profile_header" );
  HQASSERT( pInfo->filelist_head,
            "Null filelist head in query_profile_header" );
  HQASSERT( pInfo->filelist_head->file,
            "Null flptr in query_profile_header" );
  HQASSERT( !pInfo->filelist_head->next,
            "More than 1 ICC_FILELIST_INFO in query_profile_header" );

  /* Read the profile header
   * N.B. We are trying to find out if the new FILELIST refers to an ICC
   * profile, so only attempt to read from the first ICC_FILELIST_INFO.
   * In fact there should only be one ICC_FILELIST_INFO at this point,
   * see above assert.
   */
  if ( !readHeader( pInfo->filelist_head->file, &icc_header ))
    return FALSE;

  /* Do we support this version number? */
  if ( !ICSupportVersion( &icc_header ))
    return detail_error_handler( CONFIGURATIONERROR,
            "ICC profile version not currently supported." );

  /* Handle devicelink profiles mislabelled as colorspace conversion */
  if ( icc_header.deviceClass == icSigColorSpaceClass ) {

    if ( icc_header.pcs != icSigXYZData &&
         icc_header.pcs != icSigLabData )
      icc_header.deviceClass = icSigLinkClass;
  }

  /* Do we support this profile type ? */
  if ( !(icc_header.deviceClass == icSigInputClass ||
         icc_header.deviceClass == icSigDisplayClass ||
         icc_header.deviceClass == icSigOutputClass ||
         icc_header.deviceClass == icSigLinkClass ||
         icc_header.deviceClass == icSigColorSpaceClass))
    return detail_error_handler( CONFIGURATIONERROR,
            "ICC profile type not currently supported." );

  pInfo->deviceClass = icc_header.deviceClass;

  /* Fill in the colorspace if we can handle it */
  if ( !convert_colorspace( icc_header.colorSpace,
                            icc_header.deviceClass,
                            &(pInfo->devicespace)))
    return FALSE;

  /* Fill in the pcs if we can handle it */
  if ( !convert_pcs_space( &icc_header, &(pInfo->pcsspace)))
    return FALSE;

  /* Extract the number of pcs color components (and validate it) */
  pInfo->n_pcs_colors = number_colors( icc_header.pcs );
  if ( pInfo->n_pcs_colors < 1 || pInfo->n_pcs_colors > MAX_CHANNELS )
    return detail_error_handler( SYNTAXERROR,
            "Invalid PCS space in ICC profile." );

    /* Extract the number of device color components (and validate it) */
  pInfo->n_device_colors = number_colors( icc_header.colorSpace );
  if ( pInfo->n_device_colors < 1 || pInfo->n_device_colors > MAX_CHANNELS ) {
    pInfo->n_device_colors = -1;
    return detail_error_handler( SYNTAXERROR,
            "Invalid colorspace in ICC profile." );
  }

  /* Grab the rendering intent if valid */
  if ( icc_header.renderingIntent < SW_CMM_N_ICC_RENDERING_INTENTS )
    pInfo->preferredIntent = CAST_UNSIGNED_TO_UINT8(icc_header.renderingIntent);

  /* While we're here initialise the WhitePoint and RelativeWhitePoint */
  initialise_xyzinfo( icc_header.illuminant, pInfo );

  /* Record the profile size */
  pInfo->profile_size = icc_header.size;

  /* Grab the profileID */
  HqMemCpy(&(pInfo->md5[0]), &icc_header.profileID, sizeof(icProfileIdNumber));

  return TRUE;
}

/*----------------------------------------------------------------------------*/

static Bool create_1D_tables( CLINKiccbased **ppIccBasedInfo,
                              FILELIST *flptr,
                              int32 offset,
                              int8 n_curves,
                              int32 n_entries,
                              uint8 precision)
{
  int32 i;
  MI_PIECEWISE_LINEAR *piecewise_linear = NULL;
  DATA_PIECEWISE_LINEAR *current_curve, *next_curve;
  size_t piecewise_size = 0;

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in create_1D_tables" );
  HQASSERT( flptr, "Null flptr in create_1D_tables" );
  HQASSERT( n_curves > 0 && n_curves <= MAX_CHANNELS,
            "Unexpected number of curves in create_1D_tables");
  HQASSERT( precision == 1 || precision == 2,
            "Illegal precision in create_1D_tables" );


  /* Work out how much space to allocate */
  piecewise_size = MI_PIECEWISE_LINEAR_SIZE +
                    n_curves * DATA_PIECEWISE_LINEAR_SIZE( n_entries );

  /* Try to allocate it */
  piecewise_linear = mi_alloc( piecewise_size );
  if ( !piecewise_linear )
    return FALSE;

  /* Start filling in the structure to pass to the mini-invoke creator */
  piecewise_linear->channelmask = (1<<n_curves) - 1;

  /* Initialise the curve pointers */
  current_curve = &(piecewise_linear->curves[0]);
  next_curve = current_curve;

  /* Fill in the data for each DATA_PIECEWISE_LINEAR */
  for ( i = 0; i < n_curves ; i++ ) {
    current_curve = next_curve;
    current_curve->maxindex = n_entries - 1;

    if ( precision == 2 ) {
      if ( !read_curv_data( FALSE, flptr, offset, n_entries, current_curve ))
        goto tidyup;
    } else {
      if ( !read_8bit_curv_data( flptr, offset, n_entries, current_curve ))
        goto tidyup;
    }

    offset += n_entries * precision;
    next_curve = current_curve->next = (DATA_PIECEWISE_LINEAR*)
                 ((int8*)current_curve + DATA_PIECEWISE_LINEAR_SIZE(n_entries));
  }

  /* Set the last pointer to Null */
  current_curve->next = 0;

  /* Add this to the master invoke */
  if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_piecewise_linear,
                            piecewise_linear ))
    goto tidyup;

  return TRUE;

tidyup:
  mi_free(piecewise_linear);
  return FALSE;
}


/* Create the mini-invoke for the 1Ds */
static Bool create_1D_curves( CLINKiccbased **ppIccBasedInfo,
                              FILELIST *flptr,
                              int32 offset,
                              int32 n_curves )
{
  int32 i,j;
  icTagTypeSignature curve_type = 0;
  MI_PIECEWISE_LINEAR *piecewise_linear = NULL;
  MI_PARAMETRIC *parametric = NULL;
  DATA_PIECEWISE_LINEAR *current_curve, *next_curve;
  uint32 piecewise_channelmask = 0;
  uint32 parametric_channelmask = 0;
  size_t piecewise_size = 0;
  size_t parametric_size = 0;
  uint32 n_paras = 0;
  uint32 curve_offset[ MAX_CHANNELS ];
  int32 curve_length[ MAX_CHANNELS ];
  int32 curve_types[ MAX_CHANNELS ];

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in create_1D_curves" );
  HQASSERT( flptr, "Null flptr in create_1D_curves" );
  HQASSERT( n_curves > 0 && n_curves <= MAX_CHANNELS,
            "Unexpected number of curves in create_1D_curves");

  curve_offset[0] = offset;

  /* First find out the curve types of all the curves as we want to know if it
     can be handled as piecewise linear or parametric.  Store the result as a
     bitmask.  Also need to find out how much space to allocate.
  */
  for ( i = 0; i < n_curves ; i++ ) {
    /* Find out what type of curve it is */
    if ( !read_curve_type( flptr, curve_offset[i], &curve_type ))
      return FALSE;

    if ( curve_type == icSigParametricCurveType ) {
      parametric_channelmask |= (1u<<i);
      n_paras++;

      /* Work out the offset to next curve and get the detailed type */
      if ( !get_para_info( flptr, curve_offset[i], &(curve_length[i]),
                           &(curve_types[i]) ))
        return FALSE;

      curve_offset[i+1] = curve_offset[i] + PARA_SPACE( curve_length[i] );
    }
    else if ( curve_type == icSigCurveType ) {
      curve_types[i] = -1;

      /* Get the curve length - n.b. a curve length of 0 indicates a linear
       * curve so nothing to do here.
       */
      if ( !get_curv_length( flptr, curve_offset[i], &(curve_length[i]) ))
        return FALSE;

      /* Work out offset to next curve */
      curve_offset[i+1] = curve_offset[i] + CURV_SPACE( curve_length[i] );

      if ( curve_length[i] == 1 ) {
        /* We effectively have a parametric curve after all */
        parametric_channelmask |= (1u<<i);
        n_paras++;
      }
      else if ( curve_length[i] != 0) {
        /* It's piecewise linear */
        piecewise_channelmask |= (1u<<i);
        piecewise_size += DATA_PIECEWISE_LINEAR_SIZE( curve_length[i] );
      }
    }
    else {
      /* It's an invalid curve type */
      return detail_error_handler( SYNTAXERROR,
              "Invalid curve type in ICC profile." );
    }
  }

  /* Should now know which curves are which and how much space to allocate */
  if ( piecewise_size > 0 ) {
    /* Add on space for the channelmask itself */
    piecewise_size += MI_PIECEWISE_LINEAR_SIZE;

    /* Try to allocate it */
    piecewise_linear = mi_alloc( piecewise_size );
    if ( !piecewise_linear )
      return FALSE;

    /* Start filling in the structure to pass to the mini-invoke creator */
    piecewise_linear->channelmask = piecewise_channelmask;

    /* Initialise the curve pointers */
    current_curve = &(piecewise_linear->curves[0]);
    next_curve = current_curve;

    /* Fill in the data for each DATA_PIECEWISE_LINEAR */
    for ( i = 0; i < n_curves ; i++ ) {
      if ( (piecewise_channelmask & (1u<<i)) != 0 ) {
        current_curve = next_curve;
        current_curve->maxindex = curve_length[i] - 1;

        if ( !read_curv_data( FALSE, flptr, ( curve_offset[i] + curv_header_size ),
                             curve_length[i], current_curve ))
          goto tidyup;

        next_curve = current_curve->next = (DATA_PIECEWISE_LINEAR*) ((int8*)
          current_curve + DATA_PIECEWISE_LINEAR_SIZE(curve_length[i]));
      }
    }

    /* Set the last pointer to Null */
    current_curve->next = 0;

    /* Add this to the master invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_piecewise_linear,
                              piecewise_linear ))
      goto tidyup;
  }


  if ( n_paras > 0 ) {
    /* Work out how much space is required */
    parametric_size = MI_PARAMETRIC_SIZE(n_paras);

     /* Try to allocate it */
    parametric = mi_alloc( parametric_size );
    if ( parametric == NULL )
      goto tidyup;

    /* Start filling in the structure to pass to the mini-invoke creator */
    parametric->channelmask = parametric_channelmask;

    /* Fill in the data for each DATA_PARAMETRIC */
    j = 0;

    for ( i = 0; i < n_curves ; i++ ) {
      if ( (parametric_channelmask & (1u<<i)) != 0 ) {
        if ( !read_para_data( flptr, ( curve_offset[i] + 12), curve_length[i],
                              curve_types[i], &(parametric->curves[j])))
          goto tidyup;

        j++;
      }
    }

    /* Add this to the master invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_parametric, parametric ))
      goto tidyup;
  }

  return TRUE;

tidyup:
  mi_free(piecewise_linear);
  mi_free(parametric);
  return FALSE;
}


/*----------------------------------------------------------------------------*/
/* create the mini-invoke for an 8bit clut */
static Bool create_clut8(CLINKiccbased** pAction,
                         FILELIST* file, int32 offset,
                         uint8 in, uint8 out,
                         uint8* gridpoints)
{
  MI_CLUT8 *clut;
  int32 i, points;
  size_t scratchSize;

  /* calculate how many points there are in the clut and allocate */
  points = out;
  for ( i = 0; i < in; ++i ) {
    if ( gridpoints[i] < 2u )
      return detail_error_handler(SYNTAXERROR,
       "ICC profile 8-bit CLUT has dimension with too few grid points." );
    points *= gridpoints[i];
  }
  clut = mi_alloc( MI_CLUT8_SIZE(points) );
  if ( !clut )
    return FALSE;

  /* fill in the structure - see mi_clut8 for use */
  points = out;
  for ( i=in-1; i>=0; --i) {
    clut->step[i] = points;
    points *= gridpoints[i];
    clut->maxindex[i] = (uint8) (gridpoints[i] - 1);
  }
  clut->in = in;
  clut->out = out;
  clut->scratch = NULL;

  /* read in the data */
  if ( !mi_seek(file, offset) ||
       file_read(file, (uint8*) &clut->values[0], points, NULL) <= 0 ) {
    /* fail if we got less than we asked for */
    mi_free(clut);
    return detail_error_handler(IOERROR,
            "ICC profile 8-bit CLUT has too few gridpoints." );
  }

  scratchSize = sizeof(ICVALUE) * out << (in - 1);

  /** \todo Global frontend and backend scratch space is a temporary workaround
      for pipelining. */
  if ( scratchSize > MAX_SCRATCH_SIZE ) {
    mi_free(clut);
    return error_handler(UNREGISTERED);
  }

  clut->scratch = mi_alloc(scratchSize);
  if (clut->scratch == NULL) {
    mi_free(clut);
    return FALSE;
  }

  /* add to action list */
  if ( !mi_add_mini_invoke(pAction, mi_clut8, clut) ) {
    mi_free(clut->scratch);
    mi_free(clut);
    return FALSE;
  }
  return TRUE;
}


/* create the mini-invoke for a 16bit clut */
static Bool create_clut16(CLINKiccbased** pAction,
                          FILELIST* file, int32 offset,
                          uint8 in, uint8 out,
                          uint8* gridpoints)
{
  MI_CLUT16 *clut;
  int32 i, points;
  size_t scratchSize;

  /* calculate how many points there are in the clut and allocate */
  points = out;
  for ( i = 0; i < in; ++i ) {
    if ( gridpoints[i] < 2u )
      return detail_error_handler(SYNTAXERROR,
              "ICC profile 16-bit CLUT has dimension with too few gridpoints." );
    points *= gridpoints[i];
  }
  clut = mi_alloc( MI_CLUT16_SIZE(points) );
  if ( !clut )
    return FALSE;

  /* fill in the structure - see mi_clut16 for use */
  points = out;
  for ( i=in-1; i>=0; --i) {
    clut->step[i] = points;
    points *= gridpoints[i];
    clut->maxindex[i] = (uint8) (gridpoints[i] - 1);
  }
  clut->in = in;
  clut->out = out;
  clut->scratch = NULL;

  /* read in the data, and correct for endianness */
  if ( !mi_seek(file, offset) ||
       file_read(file, (uint8*) &clut->values[0], points*2, NULL) <= 0 ) {
    /* fail if we got less than we asked for */
    mi_free(clut);
    return detail_error_handler(IOERROR,
            "ICC profile 16-bit CLUT has too few gridpoints." );
  }
  for ( i=0; i<points; ++i ) {
    icfixu16(clut->values[i]);
  }

  scratchSize = sizeof(ICVALUE) * out << (in - 1);

  /** \todo Global frontend and backend scratch space is a temporary workaround
      for pipelining. */
  if ( scratchSize > MAX_SCRATCH_SIZE ) {
    mi_free(clut);
    return error_handler(UNREGISTERED);
  }

  clut->scratch = mi_alloc(scratchSize);
  if (clut->scratch == NULL) {
    mi_free(clut->scratch);
    mi_free(clut);
    return FALSE;
  }

  /* add to action list */
  if ( !mi_add_mini_invoke(pAction, mi_clut16, clut) ) {
    mi_free(clut);
    return FALSE;
  }
  return TRUE;
}


/* veneer for the above two, for old-style cluts */
static Bool create_clut8_16(CLINKiccbased** pAction,
                            FILELIST* file, int32 offset,
                            uint8 in, uint8 out,
                            uint8 numpoints, uint8 precision)
{
  uint8 gridpoints[16];
  int   i;

  for ( i=0; i<in; ++i ) {
    gridpoints[i] = numpoints;
  }

  return (precision == 1) ?
         create_clut8(pAction, file, offset, in, out, gridpoints) :
         create_clut16(pAction, file, offset, in, out, gridpoints);
}


/* veneer for the above two, for new cluts */
static Bool create_clutAtoB(CLINKiccbased** pAction,
                            FILELIST* file, int32 offset,
                            uint8 in, uint8 out)
{
  CLUTATOB_HEADER header;

  if ( !mi_seek(file,offset) ||
       file_read(file, (uint8*) &header, sizeof(CLUTATOB_HEADER), NULL) <= 0 )
    return detail_error_handler(IOERROR,
            "Unable to read CLUT in ICC profile lutAtoBType or lutBtoAType ");
  offset += sizeof(CLUTATOB_HEADER);

  return (header.precision == 1) ?
         create_clut8(pAction, file, offset, in, out, header.gridpoints) :
         create_clut16(pAction, file, offset, in, out, header.gridpoints);
}

/*----------------------------------------------------------------------------*/
/* Return a value indicating the type of matrix (see enum).
 *
 * It starts with the assumption of a trivial identity matrix, and then for
 * each matrix entry that disproves something, downgrades the type. It exits
 * early if the type degrades to COMPLEX.
 */

enum { MATRIX_COMPLEX = 0, MATRIX_SCALE_TRANS, MATRIX_SKEW_TRANS, MATRIX_TRANS,
       MATRIX_SCALE_SKEW, MATRIX_SCALE, MATRIX_SKEW, MATRIX_IDENTITY };

static int matrixType( SYSTEMVALUE matrix[3][4] )
{
   int i,j,type=MATRIX_IDENTITY;

   for ( i=0; i<3; i++ ) {
     for ( j=0; j<3; j++ ) {
       if (i==j) {
         if ( matrix[i][j] != 1.0f &&
              (type &= MATRIX_SCALE) == MATRIX_COMPLEX )
           return type;

       } else {
         if ( matrix[i][j] != 0.0f &&
              (type &= MATRIX_SKEW) == MATRIX_COMPLEX )
           return type;
       }
     }
     if ( matrix[i][3] != 0.0f &&
          (type &= MATRIX_TRANS) == MATRIX_COMPLEX )
       return type;
   }
   return type;
}


/* Add a matrix to the action list, if not an identity matrix.
 * Also spots scale-only matrices, for a slight optimisation.
 *
 * If it's identity, or there's a problem, the matrix is discarded.
 */
static Bool add_matrix( CLINKiccbased **ppIccBasedInfo, MI_MATRIX *matrix_ptr )
{
  switch ( matrixType( matrix_ptr->matrix ) ) {
  case MATRIX_IDENTITY:
    /* Identity, so there's no point adding an invoke */
    mi_free(matrix_ptr);
    break;

  case MATRIX_SCALE:
  case MATRIX_SCALE_TRANS:
    /* add a scale and offset mini-invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_scale, matrix_ptr )) {
      mi_free(matrix_ptr);
      return FALSE;
    }
    break;

  default:
    /* Add this to the master invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_matrix, matrix_ptr )) {
      mi_free(matrix_ptr);
      return FALSE;
    }
  }

  return TRUE;
}


/* Create an MI_MATRIX, initialising it to sensible values */
MI_MATRIX* new_matrix(Bool clip)
{
  int i;
  MI_MATRIX* matrix = mi_alloc( sizeof(MI_MATRIX) );
  if (matrix == NULL)
    return NULL;

  ZEROINIT(matrix,MI_MATRIX);
  for (i=0; i<3; i++)  matrix->matrix[i][i] = 1.0f;
  matrix->clip = clip; /* whether to clip output to range 0.0f to 1.0f */

  return matrix;
}


/* Create the mini_invoke for matrix from a lutAtoBType or lutBtoAType tag */
static Bool create_matrix( CLINKiccbased **ppIccBasedInfo, FILELIST* flptr,
                           int32 offset )
{
  MI_MATRIX *matrix_ptr;

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in create_matrix" );

  matrix_ptr = new_matrix(TRUE);
  if ( !matrix_ptr )
    return error_handler(VMERROR);

  /* Read the data from the file scaling as we go */
  if ( !read_matrix(flptr, offset, matrix_ptr->matrix)) {
    mi_free(matrix_ptr);
    return FALSE;
  }

  return add_matrix( ppIccBasedInfo, matrix_ptr );
}


/* This assumes the matrix is already read in */
static Bool create_3by3_matrix( CLINKiccbased **ppIccBasedInfo,
                                LUT_TEMP_DATA *data )
{
  MI_MATRIX *matrix_ptr;
  int i,j;
  icS15Fixed16Number *matrix = &(data->e00);

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in create_3by3_matrix" );

  matrix_ptr = new_matrix(TRUE);
  if ( !matrix_ptr )
    return error_handler(VMERROR);

  for (i=0; i<3; i++) {
    for (j=0; j<3; j++) {
      matrix_ptr->matrix[i][j] = matrix[i*3 + j] / 65536.0f;
    }
  }

  return add_matrix( ppIccBasedInfo, matrix_ptr );
}

/*----------------------------------------------------------------------------*/

static Bool create_flip( CLINKiccbased **ppIccBasedInfo, uint32 channels )
{
  MI_FLIP *flip;

  flip = mi_alloc( sizeof(MI_FLIP) );
  if ( flip == NULL )
    return FALSE;

  flip->channels = channels;

  if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_flip, flip)) {
    mi_free(flip);
    return FALSE;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/

/* Encode Lab such that all channels are in the range 0-1 */
Bool create_lab2icclab( CLINKiccbased **ppIccBasedInfo )
{
  MI_MATRIX *matrix_ptr = NULL;

  matrix_ptr = new_matrix(FALSE); /* don't clip the output */
  if ( !matrix_ptr ) {
    (void) error_handler(VMERROR);
    return FALSE;
  }

  /* Scales */
  matrix_ptr->matrix[0][0] = 1.0f / L_RANGE;
  matrix_ptr->matrix[1][1] = 1.0f / AB_RANGE;
  matrix_ptr->matrix[2][2] = 1.0f / AB_RANGE;

  /* Offsets */
  matrix_ptr->matrix[0][3] = 0;
  matrix_ptr->matrix[1][3] = (0 - AB_MIN_VALUE) / AB_RANGE;
  matrix_ptr->matrix[2][3] = (0 - AB_MIN_VALUE) / AB_RANGE;

  if ( !add_matrix( ppIccBasedInfo, matrix_ptr ) )
    return FALSE;

  return TRUE;
}


/* Convert ICC XYZ to CIEXYZ */
static Bool create_iccxyz2ciexyz( CLINKiccbased **ppIccBasedInfo,
                                  ICC_PROFILE_INFO *pInfo )
{
  int i;
  MI_XYZ *xyz2xyz;
  double xyz_scale = 65535.0 / 32768.0;  /* ICC XYZ to CIE XYZ */

  xyz2xyz = mi_alloc( sizeof(MI_XYZ) );
  if ( xyz2xyz == NULL )
    return FALSE;

  /* Calculate what scalings to apply */
  for ( i=0; i<3; i++ ) {
    xyz2xyz->scale[i] = xyz_scale *
                        pInfo->relative_whitepoint[i] / pInfo->whitepoint[i];
    xyz2xyz->relative_whitepoint[i] = 0.0;  /* unused */
  }

  if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_xyz2xyz, xyz2xyz)) {
    mi_free(xyz2xyz);
    return FALSE;
  }

  return TRUE;
}


/* Convert CIEXYZ to ICC XYZ */
Bool create_ciexyz2iccxyz( CLINKiccbased **ppIccBasedInfo,
                           ICC_PROFILE_INFO *pInfo )
{
  int i;
  MI_XYZ *xyz2xyz;
  double xyz_scale = 32768.0 /65535.0;  /* CIE XYZ to ICC XYZ */

  xyz2xyz = mi_alloc( sizeof(MI_XYZ) );
  if ( !xyz2xyz )
    return FALSE;

  /* Calculate what scalings to apply - this time we can put them directly
     into the CLINKiccbased */
  for ( i=0; i<3; i++ ) {
    xyz2xyz->scale[i] = xyz_scale *
                        pInfo->whitepoint[i] / pInfo->relative_whitepoint[i];
    xyz2xyz->relative_whitepoint[i] = 0.0;  /* unused */
  }

  if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_xyz2xyz, xyz2xyz)) {
    mi_free(xyz2xyz);
    return FALSE;
  }

  return TRUE;
}


enum { OUTPUT_LAB = 0, OUTPUT_OLD_LAB,
       OUTPUT_XYZ, OUTPUT_CIE_XYZ,
       OUTPUT_CMY, OUTPUT_OTHER, OUTPUT_NONE,
       INPUT_CONVERSION_LAB,
       INPUT_LAB, INPUT_OLD_LAB,
       INPUT_XYZ, INPUT_CIE_XYZ,
       INPUT_CMY, INPUT_OTHER, INPUT_NONE };


/* this creator can be used for input (lab to xyz) or output (xyz to lab) */

static Bool create_icclab2ciexyz( CLINKiccbased **ppIccBasedInfo,
                                  ICC_PROFILE_INFO *pInfo,
                                  int transformType )
{
  int i;
  MI_XYZ *lab2xyz;
  MINI_INVOKE invoke = mi_lab2xyz;

  lab2xyz = mi_alloc( sizeof(MI_XYZ) );
  if ( lab2xyz == NULL )
    return FALSE;

  /* Pass in scale value - this makes invoke calculation quicker */
  lab2xyz->scale[0] = 100/116.0f;
  lab2xyz->scale[1] = 255/500.0f;
  lab2xyz->scale[2] = 255/200.0f;

  /* If we had a legacy lut16Type encoding we need to scale the data */
  if ( transformType == OUTPUT_OLD_LAB || transformType == INPUT_OLD_LAB) {
    for (i=0;i<3;i++)
      lab2xyz->scale[i] *= 65535/65280.0f;
  }

  if ( transformType == INPUT_LAB || transformType == INPUT_OLD_LAB )
    invoke = mi_xyz2lab;

  for (i=0;i<3;i++)
    lab2xyz->relative_whitepoint[i] = pInfo->relative_whitepoint[i];

  if ( !mi_add_mini_invoke( ppIccBasedInfo, invoke, lab2xyz)) {
    mi_free(lab2xyz);
    return FALSE;
  }

  return TRUE;
}

/*---------------------------------------------------------------------------*/
/* Following adapted from icfix.c */

/* Rescale the conversion from xyz to lab or vice versa.  Called to correct
 * a misinterpretation of the icc spec re Lab PCS encoding.
 */
static Bool rescale_icclab2ciexyz( CLINKiccbased *pIccBasedInfo,
                                   double *fix_factors,
                                   int transformType )
{
  int i;
  MI_XYZ *lab2xyz = NULL;

  HQASSERT( transformType == OUTPUT_OLD_LAB ||
            transformType == INPUT_LAB || transformType == INPUT_OLD_LAB,
            "Unexpected transform type in rescale_icclab2ciexyz" );

  HQASSERT( fix_factors[0] != 0 && fix_factors[1] != 0 && fix_factors[2] != 0,
            "Zero factor in rescale_icclab2ciexyz" );

  /* Find the correct data */
  if ( transformType == INPUT_OLD_LAB || transformType == INPUT_LAB ) {
    if ( pIccBasedInfo->actions[0].function != mi_xyz2lab )
      return detail_error_handler( CONFIGURATIONERROR,
              "Problem handling output profile PCS to Lab mapping." );

    lab2xyz = pIccBasedInfo->actions[0].u.lab2xyz;
  }
  else {
    for ( i=0; pIccBasedInfo->actions[i].function != 0; i++ ) {
      if ( pIccBasedInfo->actions[i].function == mi_lab2xyz ) {
        lab2xyz = pIccBasedInfo->actions[i].u.lab2xyz;
        break;
      }
    }

    if ( !lab2xyz )
      return detail_error_handler( CONFIGURATIONERROR,
              "Problem handling input profile Lab to PCS mapping." );
  }

  /* Adjust the scaling */
  if ( transformType == INPUT_OLD_LAB || transformType == INPUT_LAB )
    for (i=0;i<3;i++)
      lab2xyz->scale[i] = lab2xyz->scale[i] / fix_factors[i];
  else
    for (i=0;i<3;i++)
      lab2xyz->scale[i] = lab2xyz->scale[i] * fix_factors[i];

  return TRUE;
}


/* Check if a DATA_PIECEWISE_LINEAR curve is at least weakly monotonic */
static Bool check_curve_monotonic( DATA_PIECEWISE_LINEAR *curve )
{
  uint32 i;
  Bool ascending = FALSE;
  Bool monotonic = TRUE;

  if ( curve->values[curve->maxindex] >= curve->values[0] )
    ascending = TRUE;

  if ( ascending ) {
    for ( i = 0; i < curve->maxindex; i++ ) {

      if ( curve->values[i+1] < curve->values[i] ) {
        monotonic = FALSE;
        break;
      }
    }
  }
  else {    /* descending */
    for ( i = 0; i < curve->maxindex; i++ ) {

      if ( curve->values[i+1] > curve->values[i] ) {
        monotonic = FALSE;
        break;
      }
    }
  }

  return monotonic;
}


/* This function probes an input lut16Type to check for misinterpretations of
 * the ICC spec whereby FFFF is used to encode either just for for maximum L
 * value or for all of L, a, and b instead of FF00, (i.e. whereby zero a and
 * b values code to 8080 instead of 8000).
 */
static Bool icc_probe_input_profile( CLINKiccbased *pIccBasedInfo )
{
#define L100   (float)(65280/65535.0)       /* FF00h/FFFFh = 0.9961089494163 */
#define A0     (float)(32768/65535.0)       /* 8000h/FFFFh = 0.5000076295109 */
#define B0     (float)(32768/65535.0)       /* 8000h/FFFFh = 0.5000076295109 */
#define CB_A0  (float)(128/255.0)           /* 80h/FFh = 0.5019607843137 */
#define CB_B0  (float)(128/255.0)           /* 80h/FFh = 0.5019607843137 */
#define L_TOL  (float)(255/(2*65535.0))     /* 0.5 FFh parts in FFFFh */
#define AB_TOL (float)(64/65535.0)          /* 0.5 80h parts in FFFFh */

typedef struct
{
  USERVALUE expected[3];
  double factors[3];
} icc_ip_probe;

  int32 i, fix, clut_pos, start_pos, n_actions = 3;
  Bool match_found = FALSE;

  USERVALUE input[MAX_CHANNELS];
  USERVALUE output[MAX_CHANNELS];

  icc_ip_probe probes[3] = {{{L100, A0,    B0   }, {1.0f, 1.0f,     1.0f    }},  /* correct profile */
                            {{1.0f, A0,    B0   }, {L100, 1.0f,     1.0f    }},  /* FFFF for L vals */
                            {{1.0f, CB_A0, CB_B0}, {L100, A0/CB_A0, B0/CB_B0}}}; /* FFFF for L,a,b  */

  iccbasedInfoAssertions( pIccBasedInfo );
  HQASSERT( pIccBasedInfo->oColorSpace == SPACE_ICCLab,
            "Expected Lab colorspace in icc_probe_input_profile" );

  /* Identify the CLUT */
  clut_pos = 0;

  while ( ( pIccBasedInfo->actions[clut_pos].function != NULL ) &&
          ( pIccBasedInfo->actions[clut_pos].function != mi_clut16 )) {
    clut_pos++;
  }

  HQASSERT( pIccBasedInfo->actions[clut_pos].function != NULL,
            "Unable to identify CLUT in icc_probe_input_profile" );

  start_pos = clut_pos;

  /* We normally have to probe 1D input curves then the CLUT then 1D output
   * curves.  But perhaps we have e.g. optimised linear curves away.
   * First see if we have 1D input curves
   */
  if ( ( clut_pos >= 1 ) &&
       ( pIccBasedInfo->actions[clut_pos - 1].function == mi_piecewise_linear ))
    start_pos = clut_pos - 1;
  else
    n_actions--;

  /* See if we have 1D output curves - else there will be a NULL or other function */
  if ( pIccBasedInfo->actions[clut_pos + 1].function != mi_piecewise_linear )
    n_actions--;


  /* Try assuming the colorspace is subtractive */
  for ( i = 0; i < pIccBasedInfo->i_dimensions; i++ )
    input[i] = 0.0f;

  /* Probe the lut16Type */
  if ( !iccbased_invokeActions( pIccBasedInfo,
                                start_pos,
                                n_actions,
                                pIccBasedInfo->i_dimensions,
                                3,
                                input,
                                output ))
    return FALSE;

  for ( fix = 0; fix < 2; fix++ ) {

    if ( ( fabs( output[0] - probes[fix].expected[0] ) < L_TOL ) &&
         ( fabs( output[1] - probes[fix].expected[1] ) < AB_TOL ) &&
         ( fabs( output[2] - probes[fix].expected[2] ) < AB_TOL ) ) {

      match_found = TRUE;
      break;
    }
  }

  if ( !match_found ) {
    /* Perhaps the space is additive */
    for ( i = 0; i < pIccBasedInfo->i_dimensions; i++ )
      input[i] = 1.0f;

    /* Probe the lut16Type */
    if ( !iccbased_invokeActions( pIccBasedInfo,
                                  start_pos,
                                  n_actions,
                                  pIccBasedInfo->i_dimensions,
                                  3,
                                  input,
                                  output ))
      return FALSE;

    for ( fix = 0; fix < 2; fix++ ) {

      if ( ( fabs( output[0] - probes[fix].expected[0] ) < L_TOL ) &&
           ( fabs( output[1] - probes[fix].expected[1] ) < AB_TOL ) &&
           ( fabs( output[2] - probes[fix].expected[2] ) < AB_TOL ) ) {

        match_found = TRUE;
        break;
      }
    }
  }


  /* Redo the Lab to XYZ scaling if necessary */
  if ( match_found && fix != 0 )
    if (!rescale_icclab2ciexyz( pIccBasedInfo, probes[fix].factors, OUTPUT_OLD_LAB ))
      return FALSE;

  return TRUE;

#undef L100
#undef A0
#undef B0
#undef CB_A0
#undef CB_B0
#undef L_TOL
#undef AB_TOL
}


/* This function probes an output lut16Type to check for common
 * misinterpretations about the Lab encoding.  In this tag type FF00 should
 * be used to code for maximum Lab values, whereas some profile producers
 * assumed FFFF as the maximum either just for L values or for all of
 * L, a and b.
 */
static Bool icc_probe_output_profile( CLINKiccbased *pIccBasedInfo )
{
#define L100  (float)(65280/65535.0)        /* FF00h/FFFFh = 0.9961089494163 */
#define A0    (float)(32768/65535.0)        /* 8000h/FFFFh = 0.5000076295109 */
#define B0    (float)(32768/65535.0)        /* 8000h/FFFFh = 0.5000076295109 */
#define CB_A0 (float)(128/255.0)            /* 80h/FFh = 0.5019607843137 */
#define CB_B0 (float)(128/255.0)            /* 80h/FFh = 0.5019607843137 */

#define DevC (1.0/255)  /* Tolerance of 100% of a 256-level device code */
#define HDvC (DevC/2)   /* Tolerance of  50% of a 256-level device code */

typedef struct
{
  USERVALUE input[3];
  double factors[3];
} icc_op_probe;

  int32 i, fix, clut_pos, start_pos, n_actions = 3;
  Bool match_found = FALSE;
  USERVALUE output[MAX_CHANNELS];

  icc_op_probe probes[3] = {{{L100, A0,    B0   }, {1.0f,   1.0f,     1.0f    }},   /* correct profile */
                            {{1.0f, A0,    B0   }, {1/L100, 1.0f,     1.0f    }},   /* L vals to FFFF */
                            {{1.0f, CB_A0, CB_B0}, {1/L100, CB_A0/A0, CB_B0/B0}}};  /* Lab all to FFFF */

  iccbasedInfoAssertions( pIccBasedInfo );
  HQASSERT( pIccBasedInfo->iColorSpace == SPACE_ICCLab,
            "Expected Lab colorspace in icc_probe_output_profile" );

  /* Identify the CLUT */
  clut_pos = 0;

  while ( ( pIccBasedInfo->actions[clut_pos].function != NULL ) &&
          ( pIccBasedInfo->actions[clut_pos].function != mi_clut16 )) {
    clut_pos++;
  }

  HQASSERT( pIccBasedInfo->actions[clut_pos].function != NULL,
            "Unable to identify CLUT in icc_probe_output_profile" );
  HQASSERT( clut_pos >= 1,
            "Unexpected CLUT position in icc_probe_output_profile" );

  start_pos = clut_pos;

  /* We should have CIEXYZ to ICCLab as the first action, (which is not probed).
   * Then we normally have to probe 1D input curves then the CLUT then 1D
   * output curves.  But perhaps in future we may have e.g. optimised linear
   * curves away.
   * First see if we have 1D input curves
   */
  if ( ( clut_pos >= 2 ) &&
       ( pIccBasedInfo->actions[clut_pos - 1].function == mi_piecewise_linear ))
    start_pos = clut_pos - 1;
  else
    n_actions--;

  /* See if we have 1D output curves - else there will be a NULL or other function */
  if ( pIccBasedInfo->actions[clut_pos + 1].function != mi_piecewise_linear )
    n_actions--;

  for ( fix = 0; fix < 3; fix++ ) {

    /* Probe the lut16Type */
    if ( !iccbased_invokeActions( pIccBasedInfo,
                                  start_pos,
                                  n_actions,
                                  3,
                                  pIccBasedInfo->o_dimensions,
                                  probes[fix].input,
                                  output ))
      return FALSE;

    for ( i = 0; i < pIccBasedInfo->o_dimensions; i++ ) {

      /* Check whether each component is additive or subtractive
       * in case of weird RGB with spots or similar.
       */
      if ( fabs( output[i] ) > 0.5f ) {
        /* additive */
        if ( fabs( 1.0f - output[i] ) > HDvC )
          break;
      }
      else {
        /* subtractive */
        if ( fabs( output[i] ) > HDvC )
          break;
      }
    }

    if ( i == pIccBasedInfo->o_dimensions ) {
      match_found = TRUE;
      break;
    }
  }

  /* We need to redo the XYZ to Lab scaling if necessary */
  if ( match_found && fix != 0 )
    if ( !rescale_icclab2ciexyz( pIccBasedInfo, probes[fix].factors, INPUT_OLD_LAB ))
      return FALSE;

  return TRUE;

#undef L100
#undef A0
#undef B0
#undef CB_A0
#undef CB_B0
#undef DevC
#undef HDvC
}


/* This function tries to find the whitepoint in the clut grid for lut8Type
 * output profiles (as these can have quantisation problems).  Having found
 * it, it then goes backwards through the input tables to find the correct
 * input for white, and rescales the XYZ to Lab conversion if necessary.
 * If it is unable to find a whitepoint (e.g. due to limitations of the code
 * here), it returns TRUE, and no change is made to the CLINKiccbased data.
 */
static Bool icc_probe_whitepoint( CLINKiccbased **ppIccBasedInfo )
{
/* These are L*a*b* values as they are represented in the 8-bit 'mft1' ICC domain */
#define l100 1.0f                             /* FFh/FFh = 1.0 */
#define ab0  (float)(128/255.0)               /* 80h/FFh = 0.5019607843137 */

#define DevC (1.0/256)  /* Tolerance of 100% of a 256-level device code */
#define HDvC (DevC/2)   /* Tolerance of  50% of a 256-level device code */

  CLINKiccbased *pIccBasedInfo;

  int32 i, j, k;
  int32 clut_pos, start_pos, n_actions = 3;
  int32 n_out_actions;
  int32 a_val[2], b_val[2];
  Bool output_curves_present = FALSE;
  Bool monotonic = TRUE;
  Bool white_found = FALSE;

  size_t parametric_size = 0;
  MI_PARAMETRIC *parametric = NULL;
  MI_PIECEWISE_LINEAR* piecewise_linear;
  DATA_PIECEWISE_LINEAR *curve;
  DATA_PARAMETRIC *para;

  USERVALUE L100plane, max;
  USERVALUE input[3], output[MAX_CHANNELS];
  USERVALUE probe[3] = {1.0f, ab0, ab0};        /* correct profile */

  HQASSERT( ppIccBasedInfo, "Null ppIccBasedInfo in icc_probe_whitepoint" );
  pIccBasedInfo = *ppIccBasedInfo;

  iccbasedInfoAssertions( pIccBasedInfo );
  HQASSERT( pIccBasedInfo->iColorSpace == SPACE_ICCLab,
            "Expected Lab colorspace in icc_probe_whitepoint" );

  /* Confirm the CLUT position */
  clut_pos = 0;

  while ( ( pIccBasedInfo->actions[clut_pos].function != NULL ) &&
          ( pIccBasedInfo->actions[clut_pos].function != mi_clut8 )) {
    clut_pos++;
  }

  HQASSERT( pIccBasedInfo->actions[clut_pos].function != NULL,
            "Unable to identify CLUT in icc_probe_whitepoint" );

  start_pos = clut_pos;

  /* We will normally have CIEXYZ to ICCLab as the first action, (which is not
   * probed).  Then we normally have to probe 1D input curves then the CLUT
   * then 1D output curves.  But perhaps in future we may have e.g. optimised
   * linear curves away, (and the CIEXYZ to ICCLab is not strictly needed here).
   * First see if we have 1D input curves
   */
   if ( ( clut_pos >= 1 ) &&
        ( pIccBasedInfo->actions[clut_pos - 1].function == mi_piecewise_linear ))
     start_pos = clut_pos - 1;
   else
     n_actions--;

  /* See if we have 1D output curves */
  if ( pIccBasedInfo->actions[clut_pos + 1].function != mi_piecewise_linear )
    n_actions--;
  else
    output_curves_present = TRUE;

  /* Probe the lut8Type */
  if ( !iccbased_invokeActions( pIccBasedInfo,
                                start_pos,
                                n_actions,
                                3,
                                pIccBasedInfo->o_dimensions,
                                probe,
                                output ))
    return FALSE;

  for ( i = 0; i < pIccBasedInfo->o_dimensions; i++ ) {

    /* Check whether each component is additive or subtractive
     * in case of weird RGB with spots or similar.
     */
    if ( fabs( output[i] ) > 0.5f ) {
      /* additive */
      if ( fabs( 1.0f - output[i] ) > HDvC )
        break;
    }
    else {
      /* subtractive */
      if ( fabs( output[i] ) > HDvC )
        break;
    }
  }

  if ( i == pIccBasedInfo->o_dimensions )
    return TRUE;      /* whitepoint is ok */


  /* The whitepoint was not where it was expected, so try to find a clut
   * gridpoint corresponding to the whitepoint.
   */

  /* First probe the input tables if present */
  for ( i = 0; i < 3; i++ )
    output[i] = probe[i];

  if ( start_pos < clut_pos ) {
    if ( !iccbased_invokeActions( pIccBasedInfo, start_pos, 1, 3, 3, probe, output ))
      return FALSE;
  }

  /* Give up if the L100 plane isn't on one edge of the clut cube */
  if ( ( fabs( output[0] ) > HDvC ) &&
       ( fabs( 1.0f - output[0] ) > HDvC ))
    return TRUE;

  /* Record which is the L00plane */
  L100plane = output[0];

  /* Find the nearest gridpoints to the probe */
  max = pIccBasedInfo->actions[clut_pos].u.clut8->maxindex[0];  /* same each side of lut8Type clut */
  a_val[0] = (int32) ( output[1]  *  max );
  b_val[0] = (int32) ( output[2]  *  max );
  a_val[1] = a_val[0] + 1;
  b_val[1] = b_val[0] + 1;

  /* Probe to see if the whitepoint is one of the 4 nearest points */
  input[0] = L100plane;
  n_out_actions = output_curves_present ? 2 : 1;

  for ( i = 0; ( i < 2 && !white_found ); i++ ) {
    for ( j = 0; j < 2; j++ ) {

      input[1] = a_val[i]/max;
      input[2] = b_val[j]/max;

      /* Probe the clut and the output tables if present */
      if ( !iccbased_invokeActions( pIccBasedInfo,
                                    clut_pos,
                                    n_out_actions,
                                    3,
                                    pIccBasedInfo->o_dimensions,
                                    input,
                                    output ))
        return FALSE;

      for ( k = 0; k < pIccBasedInfo->o_dimensions; k++ ) {
        /* We are still pretty near white so this should usually be ok */
        if ( fabs( output[k] ) > 0.5f ) {
          /* additive */
          if ( fabs( 1.0f - output[k] ) > HDvC )
            break;
        }
        else {
          /* subtractive */
          if ( fabs( output[k] ) > HDvC )
            break;
        }
      }

      if ( k == pIccBasedInfo->o_dimensions ) {
        white_found = TRUE;
        break;
      }
    }
  }

  /* Give up if we haven't found a CLUT gridpoint corresponding to white */
  if ( !white_found )
    return TRUE;

  /* Now we know the position of the whitepoint in the clut try to find the
   * corresponding position in the input tables, (if present).  To do this we
   * need to go backwards through the input tables so check first if they
   * are monotonic.
   */

  if ( start_pos < clut_pos ) {
    piecewise_linear = pIccBasedInfo->actions[start_pos].u.piecewise_linear;

    curve = piecewise_linear->curves;

    for ( i = 0; i < pIccBasedInfo->i_dimensions; i++ ) {
      monotonic = monotonic && check_curve_monotonic( curve );
      curve = curve->next;
    }

    if ( !monotonic )
      return TRUE;   /* give up */

    /* Temporarily replace the function with its inverse */
    pIccBasedInfo->actions[start_pos].function = mi_inverse_linear;

    /* Invoke the inverse linear */
    if ( !iccbased_invokeActions( pIccBasedInfo, start_pos, 1, 3, 3, input, output ))
      return FALSE;

    /* Restore the CLINKiccbased */
    pIccBasedInfo->actions[start_pos].function = mi_piecewise_linear;
  }
  else {
    for ( i = 0; i < 3; i++ )
      output[i] = input[i];
  }

  /* Add a mini_invoke containing a gamma function to rescale the data before
   * the input curves (or clut if input curves are not present).
   * This defines a curve made of 2 straight line segments through the points
   * (0,0), (ab0, output[i]), (1,1), thereby remapping inputs which would have
   * corresponded to ab0 where the whitepoint should have been, to output[i]
   * where the whitepoint actually is for the channel involved.  Other points
   * will obviously get remapped also, but the largest change occurs at the
   * place where the whitepoint should have been.  (It would equally be
   * possible to use a gamma function to produce e.g. a quadratic through these
   * points if that turns out to be necessary).
   */

  /* Work out how much space is required */
  parametric_size = MI_PARAMETRIC_SIZE(2);

  /* Try to allocate it */
  parametric = mi_alloc( parametric_size );
  if ( parametric == NULL )
    return FALSE;

  /* Start filling in the structure to pass to the mini-invoke creator */
  parametric->channelmask = 0x06;    /* for a and b channels */

  /* Fill in the data for each DATA_PARAMETRIC */
  for ( i = 1; i < 3 ; i++ ) {       /* n.b. not for L channel */

    para = &(parametric->curves[i-1]);
    ZEROINIT(para,DATA_PARAMETRIC);
    para->gamma = 1;
    para->a = ( output[i] - 1 ) / ( ab0 - 1 );
    para->b = 1 - para->a;
    para->c = output[i] / ab0;
    para->d = ab0;
    para->e = 0;
    para->f = 0;
  }

  /* Add this to the master invoke before the linear input curves
   * (or clut if no input curves).
   */
  if ( !mi_insert_mini_invoke( ppIccBasedInfo, mi_parametric, parametric, start_pos ))
    goto tidyup;

  return TRUE;

tidyup:
  mi_free( parametric );
  return FALSE;

#undef l100
#undef ab0

#undef DevC
#undef HDvC
}

/*----------------------------------------------------------------------------*/
/* Create a new ICC_FILELIST_INFO */
static ICC_FILELIST_INFO* create_icc_filelist( void )
{
  ICC_FILELIST_INFO *filelist;
  size_t structSize;

  structSize = sizeof( ICC_FILELIST_INFO );
  filelist = mi_alloc( structSize );
  if (filelist == NULL)
    return NULL;

  ZEROINIT(filelist,ICC_FILELIST_INFO);

  /* Initialise the non-zero values */
  filelist->sid = -1;
  filelist->orig_sid = -1;

  return filelist;
}


/* Create a new ICC_PROFILE_INFO with one ICC_FILELIST_INFO */
static ICC_PROFILE_INFO *create_icc_info( void )
{
  ICC_PROFILE_INFO *pInfo;
  ICC_FILELIST_INFO *filelist;
  size_t structSize;

  /* Create the ICC_PROFILE_INFO */
  structSize = sizeof( ICC_PROFILE_INFO );
  pInfo = mi_alloc( structSize );
  if (pInfo == NULL)
    return NULL;

  ZEROINIT(pInfo, ICC_PROFILE_INFO);

  /* Create and attach the ICC_FILELIST_INFO */
  filelist = create_icc_filelist();

  if (!filelist) {
    mi_free( pInfo );
    return NULL;
  }

  pInfo->filelist_head = filelist;

  return pInfo;
}


/* Initialise the ICC_FILELIST_INFO, and rewind the file stream in preparation
 * for adding the RSD if necessary.
 */
static Bool initialise_icc_file( ICC_FILELIST_INFO* listinfo, FILELIST* flptr)
{
  HQASSERT(listinfo, "Null cache pointer");

  if ( !isIOpenFile(flptr) ) {
    listinfo->file = NULL;
    return detail_error_handler( IOERROR, "Unable to open ICC profile." );
  }

  if (listinfo->orig_file == NULL) {
    HQASSERT(listinfo->file == NULL &&
             listinfo->sid == -1 &&
             listinfo->orig_sid == -1, "We have a file without an orig_file");
    listinfo->file = flptr;
    listinfo->orig_file = flptr;

    /** \todo - @@JJ
     * We are going to rely on removing cache entries when restoring the file
     * if it hasn't been removed by the context closing. The save level isn't
     * applicable to PDF streams, so this is something of a hack which will
     * have to be replaced. It seems good enough for the moment.
     */
    listinfo->sid = flptr->sid;
    listinfo->orig_sid = flptr->sid;
  }
  else {
    HQASSERT(listinfo->file == flptr &&
             listinfo->sid >= listinfo->orig_sid &&
             (listinfo->orig_file != flptr || listinfo->sid == listinfo->orig_sid),
             "Inconsistent ICC file ptrs");
  }

  if (isIRealFile(flptr) || isIRSDFilter(flptr) || isIRewindable(flptr)) {
    /* Rewind the file stream if the file is at EOF because it's possible that the
     * file is open but at EOF in which case the RSD won't be created properly.
     */
    if (!mi_seek(flptr, 0))
      return FALSE;
  }
  else
    HQASSERT(flptr->count == 0, "non-rewindable ICC profile not at start");

  return TRUE;
}


/* this is used by both get_cached_icc_info and find_cached_icc_info to
 * maintain the FILELIST pointer in ICC_FILELIST_INFO of the profile cache -
 * there are two, the orig_file passed in, and an RSD if orig_file is
 * not seekable or rewindable. This routine creates the RSD if necessary.
 *
 * NB that once an ICC_FILELIST_INFO has been found, all file access
 * MUST use ->file and NOT the FILELIST used to identify it in the first place
 * - they may NOT be the same!
 *
 * If it is not possible to create the RSD, the ->file is set to zero here.
 */
static Bool set_icc_file( ICC_FILELIST_INFO* listinfo, FILELIST* flptr )
{
  HQASSERT(listinfo, "Null cache pointer");
  HQASSERT(flptr != NULL, "Null filelist pointer");

  if ( !initialise_icc_file(listinfo, flptr))
    return FALSE ;

  /* if flptr is unseekable, add an RSD and update
   * listinfo->file */
  if ( !(isIRealFile(flptr) || isIRSDFilter(flptr)
#ifdef IC_NO_RSD
         || isIRewindable(flptr)
#endif
        ) ) {
    OBJECT ofile = OBJECT_NOTVM_NOTHING, rsdfile = OBJECT_NOTVM_NOTHING;
    FILELIST * rsd;

    file_store_object(&ofile, flptr, LITERAL);
    rsd = filter_standard_find(NAME_AND_LENGTH("ReusableStreamDecode"));
    HQASSERT(rsd,"Can't find RSD filter");
    if ( !push(&ofile, &operandstack) ||
         !filter_create_object(rsd, &rsdfile, NULL, &operandstack) ) {
      listinfo->file = NULL;
      return FALSE;
    }
    listinfo->file = oFile( rsdfile );
    listinfo->sid = get_core_context_interp()->savelevel;
  }
  return TRUE;
}


/* Upon a restore, when the FILELIST containing the profile is about to be
 * discarded, we detach the icc filelist info from the FILELIST and
 * keep it if we have a uniqueID. This routine clears the stored FILELIST
 * from the cache, and discards our RSD if we added one.
 */
static void clear_icc_filelist( ICC_FILELIST_INFO *listinfo, Bool rsdOnly )
{
  HQASSERT(listinfo,"Null listinfo pointer in clear_icc_filelist");

  if ( listinfo->file ) {
    if ( listinfo->file != listinfo->orig_file ) {
      /* there's an RSD, so discard it */
      (void)listinfo->file->myclosefile( listinfo->file, CLOSE_FORCE );

      /* If the underlying file remains open, we can recreate the RSD later */
      listinfo->file = listinfo->orig_file;
      listinfo->sid = listinfo->orig_sid;
    }

    if (!rsdOnly) {
      listinfo->file = NULL;
      listinfo->sid = -1;
      listinfo->orig_file = NULL;
      listinfo->orig_sid = -1;
    }
  }
}

/* Discard DeviceN colorspace objects on a restore. */
static void clear_icc_file( ICC_PROFILE_INFO *pInfo )
{
  HQASSERT(pInfo,"Null cache pointer in clear_icc_file");

  if ( oType( pInfo->dev_DeviceNobj) != ONULL )
    object_store_null(&pInfo->dev_DeviceNobj);

  if ( oType( pInfo->pcs_DeviceNobj) != ONULL )
    object_store_null(&pInfo->pcs_DeviceNobj);

  return;
}


/* Free the colorant arrays and reset the DeviceN sid */
static void clear_colorants( ICC_PROFILE_INFO *pInfo )
{
  mi_free( pInfo->device_colorants );
  pInfo->device_colorants = 0;

  mi_free( pInfo->pcs_colorants );
  pInfo->pcs_colorants = 0;

  pInfo->devNsid = -1;
}


/* Insert the (detached) ICC_FILELIST_INFO provided after the 'after'
 * ICC_FILELIST_INFO (if provided), or alternatively after any
 * ICC_FILELIST_INFO elements in the ICC_PROFILE_INFO with an open file.
 */
static void insert_listinfo( ICC_PROFILE_INFO *profile,
                             ICC_FILELIST_INFO *listinfo,
                             ICC_FILELIST_INFO *after )
{
  HQASSERT( profile != NULL, "Null profile in insert_listinfo" );
  HQASSERT( listinfo != NULL, "Null listinfo in insert_listinfo" );

  if (!after) {
    /* Find the last active listinfo */
    if ( profile->filelist_head && profile->filelist_head->file ) {

      after = profile->filelist_head;
      while (after->next && after->next->file)
        after = after->next;
    }
  }

  if (after) {
    listinfo->next = after->next;
    after->next = listinfo;

  } else {
    /* This is the only or first listinfo! */
    listinfo->next = profile->filelist_head;
    profile->filelist_head = listinfo;
  }
}


/* Push the (detached) profile provided (which is assumed to have no
 * ICC_FILELIST_INFOs) down the list after the 'after' profile (if provided),
 * or alternatively after any ICC_PROFILE_INFO elements which do contain
 * at least one ICC_FILELIST_INFO.
 */
static void demote_profile( COLOR_STATE *colorState,
                            ICC_PROFILE_INFO_CACHE *cacheItem,
                            ICC_PROFILE_INFO_CACHE *after )
{
  ICC_PROFILE_INFO *profile;

  HQASSERT( cacheItem, "Null cacheItem in demote_profile" );

  profile = cacheItem->d;
  HQASSERT( !profile->filelist_head,
            "Unexpected filelist head in demote_profile" );

  if (!after) {
    /* This is the first demotion, so find the last profile info
     * containing at least one ICC_FILELIST_INFO.
     */
    if ( colorState->ICC_cacheHead != NULL &&
         colorState->ICC_cacheHead->d->filelist_head != NULL ) {
      after = colorState->ICC_cacheHead;
      while (after->next != NULL && after->next->d->filelist_head != NULL)
        after = after->next;
    }
  }

  if (after) {
    /* Insert after that profile */
    cacheItem->next = after->next;
    after->next = cacheItem;

  } else {
    /* This is the only or first profile info cache! */
    cacheItem->next = colorState->ICC_cacheHead;
    colorState->ICC_cacheHead = cacheItem;
  }
}


/* Compare two ICC_PROFILE_ID structures */
static Bool profile_IDs_match( ICC_PROFILE_ID *uniqueID1,
                               ICC_PROFILE_ID *uniqueID2 )
{
  HQASSERT( uniqueID1 && uniqueID2,
            "Null profile ID in profile_IDs_match" );

  return (( uniqueID2->xref == uniqueID1->xref ) &&
          ( uniqueID2->contextID == uniqueID1->contextID ));
}


/* Return the filelist associated with an ICC file object */
static FILELIST *fileObjToflptr(OBJECT *iccFileObj)
{
FILELIST *flptr;

  /* check the source */
  if (oType(*iccFileObj) != OFILE) {
    (void) detail_error_handler( TYPECHECK, "Invalid ICCBased colorspace." );
    return NULL;
  }
  flptr = oFile(*iccFileObj);
  HQASSERT( flptr, "Null flptr in get_cached_icc_info" );

  if (!isIInputFile(flptr)) {
    (void) detail_error_handler(IOERROR, "Unable to open ICC profile." );
    return NULL;
  }

  return flptr;
}

/* Find an ICC_PROFILE_INFO tagged by uniqueID or filestream from the list of
 * profiles hanging off colorState.
 *
 * N.B. The order of ICC_FILELIST_INFOs should be:
 * 1) Ones with open file and orginal filestreams
 * 2) Ones with file and orig_file filestreams both closed
 * 3) Ones where orig_file is open but file is closed, i.e where we have tried
 *    but failed to add an RSD.
 *
 * If a match is found by filestream leave the ICC_FILELIST_INFO order alone.
 * If one is found by uniqueID, reopen the filestream if orig_file was closed,
 * otherwise create a new ICC_FILELIST_INFO.
 * If all is well promote the ICC_FILELIST_INFO to just after the last one with
 * an open file.
 *
 * Any time we get a match promote the ICC_PROFILE_INFO.
 */
static ICC_PROFILE_INFO *find_cached_icc_info( COLOR_STATE    *colorState,
                                               OBJECT         *iccFileObj,
                                               ICC_PROFILE_ID *uniqueID )
{
  ICC_PROFILE_INFO_CACHE *cacheItem, *previous = 0;
  ICC_FILELIST_INFO *listinfo, *prevlist = 0, *listIDmatch = 0, *after = 0;
  ICC_FILELIST_INFO *listIDprev = 0;
  Bool matchfound = FALSE, flptrmatch = FALSE;
  FILELIST *flptr;
  ICC_PROFILE_INFO_CACHE *cacheHead;

  HQASSERT( iccFileObj != NULL, "Null iccFileObj in find_cached_icc_info" );

  cacheHead = colorState->ICC_cacheHead;

  flptr = fileObjToflptr(iccFileObj);
  if (flptr == NULL)
    return NULL;

  /* First look for a cached open file */
  /* NB we should be indexing by external filelist here, so should match
   * .orig_file, and not .file (if different). However, I'll allow it,
   * asserting that I must be getting old.
   */
  for ( cacheItem = cacheHead;
        cacheItem != NULL && cacheItem->d->filelist_head != NULL;
        cacheItem = cacheItem->next ) {

    prevlist = 0;
    after = 0;

    HQASSERT( !matchfound && !listIDmatch && !listIDprev,
              "Match already found in find_cached_icc_info" );

    for ( listinfo = cacheItem->d->filelist_head; listinfo != NULL;
          listinfo = listinfo->next ) {

      /* Note the end of the open ones */
      if (listinfo->file)
        after = listinfo;

      if (listinfo->orig_file == flptr || listinfo->file == flptr) {

        HQASSERT( profile_IDs_match( &(listinfo->uniqueID), uniqueID ) ||
                  uniqueID->xref == -1,
          "Found icc cache, but uniqueIDs wrong");
        HQASSERT( listinfo->orig_file == flptr,
          "Found icc cache by internal FILELIST, oddly");

        flptrmatch = TRUE;
        matchfound = TRUE;
        break;
      }
      else if (!listIDmatch && uniqueID->xref != -1 &&
               profile_IDs_match( &(listinfo->uniqueID), uniqueID)) {
        /* Make a note of the first listinfo with a matching ID */
        listIDmatch = listinfo;
        listIDprev = prevlist;
        matchfound = TRUE;
      }
      prevlist = listinfo;
    }

    if (matchfound) {
      if (!flptrmatch) {
        if (listIDmatch->orig_file) {
          /* Don't reuse this one as we only matched on uniqueID, ie. the file
           * pointers didn't match, so make a new ICC_FILELIST_INFO */
          listinfo = create_icc_filelist();

          if (!listinfo)
            return NULL;

          listinfo->uniqueID = *uniqueID;
        }
        else {
          HQASSERT( !listIDmatch->file,
                    "Internal FILELIST oddly open in find_cached_icc_info" );

          listinfo = listIDmatch;

          /* Detach from list */
          if (listIDprev)
            listIDprev->next = listinfo->next;
          else
            cacheItem->d->filelist_head = listinfo->next;
        }

        /* Try to (re)open the FILELIST adding an RSD if necessary.
         * N.B. if this doesn't work don't fail here as we may still be able
         * to use info from the ICC_PROFILE_INFO or another
         * ICC_FILELIST_INFO.  Demote it to the end of the list though.
         * Note no order of these is maintained.
         */
        if ( !set_icc_file( listinfo, flptr )) {
          error_clear();
          after = prevlist;
        }

        /* Move to the new position */
        insert_listinfo( cacheItem->d, listinfo, after );
      }

      if (previous) {
        /* promote the ICC_PROFILE_INFO */
        previous->next = cacheItem->next;
        cacheItem->next = colorState->ICC_cacheHead;
        colorState->ICC_cacheHead = cacheItem;
      }

      return cacheItem->d;
    }

    previous = cacheItem;
  }

  /* not found */
  return NULL;
}


/* Place a cache entry, pInfo, in the list of profiles hanging off colorState.
 * Put new entries at the head of the list because these are more likely to be
 * accessed.
 * In the hand over to the back end, we need to populate the colorState with all
 * profiles that will be used. Elsewhere in the back end, this function is a
 * no-op.
 * incRefCnt is something of a hack to allow a newly created profile to use the
 * initial refCnt of 1 because 0 isn't allowed.
 */
static Bool add_icc_info_to_cache( COLOR_STATE      *colorState,
                                   ICC_PROFILE_INFO *pInfo,
                                   Bool             incRefCnt )
{
  ICC_PROFILE_INFO_CACHE *newHead;
  ICC_PROFILE_INFO_CACHE *cacheItem;

  HQASSERT( pInfo != NULL,
            "Null ICC_PROFILE_INFO pointer in add_icc_info_to_cache" );

  /* Some clients were difficult to get a colorState into. Not to worry, the
   * profiles will be the same as those used by clients that do have a colorState.
   */
  if (colorState == NULL)
    return TRUE;

  if (colorState != frontEndColorState) {
    for (cacheItem = colorState->ICC_cacheHead; cacheItem != NULL; cacheItem = cacheItem->next) {
      if (cacheItem->d == pInfo)
        return TRUE;
    }
  }
  else {
#ifdef ASSERT_BUILD
    for (cacheItem = colorState->ICC_cacheHead; cacheItem != NULL; cacheItem = cacheItem->next) {
      if (cacheItem->d == pInfo)
        HQFAIL("Profile expected to be added to list just once");
    }
#endif
  }

  HQASSERT(IS_INTERPRETER(), "Attempting to use an ICC profile in the back end");

  newHead = mi_alloc(sizeof(ICC_PROFILE_INFO_CACHE));
  if (newHead == NULL)
    return FALSE;

  newHead->next = colorState->ICC_cacheHead;
  newHead->d = pInfo;
  if (incRefCnt)
    CLINK_RESERVE(pInfo);

  colorState->ICC_cacheHead = newHead;

  return TRUE;
}


/* Starting from the first closed filelist (closed external file), and working
 * down the list, blat it if any higher up have the same uniqueID.
 */
static void dedup_filelists( ICC_PROFILE_INFO *profile )
{
  ICC_FILELIST_INFO *listinfo, **prevlist = 0;
  ICC_FILELIST_INFO *list, **prev = 0;
  Bool IDmatch;

  HQASSERT( profile->filelist_head, "Null filelist head in dedup_filelists" );

  for ( listinfo = profile->filelist_head; listinfo; listinfo = listinfo->next ) {
    if ( !listinfo->orig_file ) {
      HQASSERT( !listinfo->file,
                "Internal filelist oddly open in dedup_filelists" );
      break;
    }
  }

  if (listinfo) {
    /* This is the first closed one */
    prevlist = &listinfo;

    for ( listinfo = *prevlist;
          listinfo && !listinfo->orig_file;
          listinfo = *prevlist ) {

      prev = &profile->filelist_head;
      IDmatch = FALSE;

      for ( list = *prev; list!= listinfo; list = *prev ) {
        if ( profile_IDs_match( &(list->uniqueID), &(listinfo->uniqueID ))) {
          /* Remove the lower one from the list and free it */
          IDmatch = TRUE;
          clear_icc_filelist( listinfo, FALSE );
          *prevlist = listinfo->next;
          mi_free( listinfo );
          break;
        }
        else {
          prev = &list->next;
        }
      }

      if ( !IDmatch )
        prevlist = &listinfo->next;
    }
  }
}


/* "Purge" all cache entries down to the slevel.
 * Actually we don't discard such cached profiles at this point. Instead we
 * detach them from their file (which will no longer be valid) so that we can
 * reattach them later if the same profile is reused. Alternatively such
 * detached profiles are prime candidates for destruction in low memory
 * situations, see below. ICC_FILELIST_INFOs with detached FILELISTs are
 * demoted down the linked list so that active ones are first.  Similarly,
 * ICC_PROFILE_INFO elements with no remaining ICC_FILELIST_INFOs are
 * also demoted.
 */
void cc_purgeICCProfileInfo( COLOR_STATE *colorState, int32 savelevel )
{
  ICC_PROFILE_INFO_CACHE *cacheItem;
  ICC_PROFILE_INFO_CACHE **previous = &colorState->ICC_cacheHead;
  ICC_PROFILE_INFO_CACHE *after = NULL;
  ICC_FILELIST_INFO *listinfo, **prevlist = NULL, *listafter = NULL;

  for ( cacheItem = *previous; cacheItem && cacheItem->d->filelist_head; cacheItem = *previous ) {
    ICC_PROFILE_INFO *profile = cacheItem->d;

    prevlist = &profile->filelist_head;
    listafter = NULL;

    for ( listinfo = *prevlist; listinfo; listinfo = *prevlist ) {

      if (listinfo->sid > savelevel) {
        /* Clear the rsd only, if present */
        clear_icc_filelist( listinfo, TRUE );
      }

      if (listinfo->orig_sid > savelevel) {
        /* Detach this listinfo from its file and remove from the list */
        clear_icc_filelist( listinfo, FALSE );
        *prevlist = listinfo->next;

        /* discard the inactive listinfo when we reach the server loop,
         * or if we can't reuse this cached data at all */
        if (savelevel == 2 || listinfo->uniqueID.xref == -1) {

          mi_free(listinfo);

        } else {
          /* We can reuse this listinfo at a later date, so move it to beyond
           * the end of the open ones.  This will normally be a demotion but
           * in the case of a listinfo where we failed to add an RSD, this
           * could be a promotion. */
          insert_listinfo( profile, listinfo, listafter );

          /* So multiple detached listinfos aren't reordered, relatively */
          listafter = listinfo;
        }

      } else {
        /* this one still active */
        prevlist = &listinfo->next;
      }
    }

    /* There's no point in keeping a closed listinfo if there's an open one
     * with the same uniqueID.  There's also no point keeping more than one
     * closed one with the same uniqueID.
     */
    if (listafter) {
      dedup_filelists( profile );
    }

    /* Clear the DeviceN objects if necessary */
    if ( profile->devNsid > savelevel ) {
      clear_icc_file( profile );

      /* Also clear the colorant array if necessary */
      if ( savelevel <= 2 ) {
        clear_colorants( profile );
      }
    }

    /* Deal with the case where there are no ICC_FILELIST_INFOs left */
    if (!profile->filelist_head) {

      /* Detach this profile from the cache */
      *previous = cacheItem->next;

      if (!profile->validMD5 || savelevel <= 2) {

        /* Discard the profile if we can't reuse this cached data at all */
        profile_discard(colorState, profile, FALSE);
        mi_free(cacheItem);

      } else {
        /* We can reuse this profile data later, so demote it. */
        demote_profile(colorState, cacheItem, after);

        /* So multiple detached profiles aren't reordered, relatively */
        after = cacheItem;
      }

    } else {
      /* this one still active */
      previous = &cacheItem->next;
    }
  }
}


/* At the end of a document, blat all ICC_FILELIST_INFOs corresponding to
 * closed (embedded) icc profiles.  Their uniqueIDs must not survive
 * this point, (as they could no longer be relied on to be unique), and they
 * then contain no other useful information.
 * At least for the present also discard any profiles with a null
 * filelist head so we don't hold onto profiles between jobs.
 */
void gsc_purgeInactiveICCProfileInfo(COLOR_STATE *colorState)
{
  ICC_PROFILE_INFO_CACHE **previous = &colorState->ICC_cacheHead;
  ICC_PROFILE_INFO_CACHE *cacheItem;
  ICC_PROFILE_INFO *profile;
  ICC_FILELIST_INFO **prevlist, *listinfo;

  for ( cacheItem = *previous; cacheItem && cacheItem->d->filelist_head;
        cacheItem = *previous ) {
    profile = cacheItem->d;

    prevlist = &profile->filelist_head;

    /* Go through looking for inactive filelists */
    for ( listinfo = *prevlist; listinfo; listinfo = *prevlist ) {

      if ( listinfo->orig_sid > -1 ) {
        prevlist = &listinfo->next;
      }
      else {
        /* Detach and discard any inactive filelists */
        *prevlist = listinfo->next;
        mi_free(listinfo);
      }
    }

    /* Deal with the case where there are no ICC_FILELIST_INFOs left */
    if (!profile->filelist_head) {

      /* Detach this profile from the cache */
      *previous = cacheItem->next;

      /* Discard the profile */
      profile_discard(colorState, profile, FALSE);
      mi_free(cacheItem);
    } else {
      /* this one still active */
      previous = &cacheItem->next;
    }
  }
}

/* Pass over the file streams belonging to ICC profiles for the given context
 * id, and call the callback function with the appropriate unique id for that
 * stream. This was added as something of a hack for PDF streams that have their
 * own lifetime and could get blown away from under the ICC cache's feet.
 */
void gsc_protectICCCache(int id, void icc_callback(void *, int), void *data)
{
  ICC_PROFILE_INFO_CACHE *cacheItem;

  for ( cacheItem = frontEndColorState->ICC_cacheHead;
        cacheItem != NULL;
        cacheItem = cacheItem->next ) {
    ICC_FILELIST_INFO *listinfo;
    ICC_PROFILE_INFO *profile = cacheItem->d;

    for ( listinfo = profile->filelist_head; listinfo; listinfo=listinfo->next ){
      if (listinfo->uniqueID.contextID == id && listinfo->uniqueID.xref != -1)
        icc_callback(data, listinfo->uniqueID.xref);
    }
  }
}

/* Discard one intent of the least used profile.  If free_open is set,
 * it will allow an open profile to be discarded, though not if it is in
 * use in a color chain. */
static Bool gsc_profilePurge(Bool free_open)
{
  ICC_PROFILE_INFO_CACHE *cacheItem;
  ICC_PROFILE_INFO *profile;
  ICC_FILELIST_INFO *listinfo;
  CLINKiccbased *icc, *discard = NULL;
  int i;

  for ( cacheItem = frontEndColorState->ICC_cacheHead;
        cacheItem != NULL;
        cacheItem = cacheItem->next ) {
    Bool lookAtIt = FALSE;

    profile = cacheItem->d;

    /** \todo not thread safe, but only using frontEndColorState */
    if (CLINK_OWNER(profile)) {
      if (free_open || profile->filelist_head == NULL) {
        lookAtIt = TRUE;
      }
      else {
        /* if all the filelists are closed it is also a detached profile */
        for ( listinfo = profile->filelist_head; listinfo; listinfo=listinfo->next ){
          if (listinfo->orig_file != NULL)
            break;
        }
        if (listinfo == NULL)
          lookAtIt = TRUE;
      }

      if (lookAtIt) {
        /* This is a detached profile, try to find a link to discard. The reason
         * there may be references remaining on the link data is the ChainCache
         * will hang on to color chains below the save level at which it was
         * created. So only get rid of data that is only referenced from 'profile'.
         */
        for (i=0; i<N_ICC_TABLES; i++) {
          if ((icc = profile->dev2pcs[i]) != NULL &&
               CLINK_OWNER(icc))
            discard = icc;
          if ((icc = profile->pcs2dev[i]) != NULL &&
               CLINK_OWNER(icc))
            discard = icc;
        }
      }
    }
  }

  /* we (may) have found the last link of the least used profile */
  if (discard != NULL) {
    iccbased_discard(discard);
  }
  return discard != NULL;
}


/** Solicit method of the ICC profile low-memory handler. */
static low_mem_offer_t *gsc_profile_solicit(low_mem_handler_t *handler,
                                            corecontext_t *context,
                                            size_t count,
                                            memory_requirement_t* requests)
{
  static low_mem_offer_t offer;
  size_t size_to_purge = 0;
  ICC_PROFILE_INFO_CACHE *cacheItem;

  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(size_t, count); UNUSED_PARAM(memory_requirement_t*, requests);

  if ( !context->is_interpreter )
    /* The cache is not thread-safe, but only the interpreter thread uses it. */
    return NULL;

  for ( cacheItem = frontEndColorState->ICC_cacheHead;
        cacheItem != NULL;
        cacheItem = cacheItem->next ) {
    ICC_PROFILE_INFO *profile = cacheItem->d;
    CLINKiccbased *icc;
    int i;
    /* Estimate size based on whether it (probably) has a CLUT. */
    size_t size = profile->n_device_colors < 4 ? 10000 : 100000;

    /** \todo not thread safe, but only using frontEndColorState */
    if (CLINK_OWNER(profile)) {
      /* May release any that are only referenced from 'profile'. */
      for ( i = 0 ; i < N_ICC_TABLES ; i++) {
        if ( (icc = profile->dev2pcs[i]) != NULL && CLINK_OWNER(icc) )
          size_to_purge += size;
        if ( (icc = profile->pcs2dev[i]) != NULL && CLINK_OWNER(icc) )
          size_to_purge += size;
      }
    }
  }
  if ( size_to_purge == 0 )
    return NULL;

  offer.pool = mm_pool_color;
  offer.offer_size = size_to_purge;
  offer.offer_cost = 0.5f; /* only read, not write */
  offer.next = NULL;
  return &offer;
}


/** Release method of the ICC profile low-memory handler. */
static Bool gsc_profile_release(low_mem_handler_t *handler,
                                corecontext_t *context,
                                low_mem_offer_t *offer)
{
  size_t current_size = mm_pool_alloced_size(mm_pool_color), target_size;
  Bool free_open = FALSE;

  UNUSED_PARAM(low_mem_handler_t *, handler);
  UNUSED_PARAM(corecontext_t*, context);

  if ( current_size <= offer->taken_size )
    target_size = 0;
  else
    target_size = current_size - offer->taken_size;

  while ( current_size >= target_size ) {
    if ( !gsc_profilePurge(free_open) ) {
      if ( !free_open )
        free_open = TRUE;
      else
        break; /* can't free no more */
    }
    /* Other threads may change the size, but not worth worrying about. */
    current_size = mm_pool_alloced_size(mm_pool_color);
  }
  return TRUE;
}


/** The ICC profile low-memory handler. */
static low_mem_handler_t gsc_profile_handler = {
  "ICC profile purge",
  memory_tier_disk, gsc_profile_solicit, gsc_profile_release, TRUE,
  0, FALSE };



/* scanICCCache - scan the ICC cache */
static mps_res_t MPS_CALL scanICCCache(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t res = MPS_RES_OK;
  ICC_PROFILE_INFO_CACHE *t;
  ICC_FILELIST_INFO *fileinfo;

  UNUSED_PARAM( void *, p );
  UNUSED_PARAM( size_t, s );

  /* Scan DeviceN colorspace objects in cache */
  for ( t = frontEndColorState->ICC_cacheHead; t != NULL; t = t->next ) {

    res = ps_scan_field( ss, &t->d->dev_DeviceNobj );
    if ( res != MPS_RES_OK ) return res;

    res = ps_scan_field( ss, &t->d->pcs_DeviceNobj );
    if ( res != MPS_RES_OK ) return res;

    for ( fileinfo = t->d->filelist_head; fileinfo != NULL; fileinfo = fileinfo->next ) {
      OBJECT tmp = OBJECT_NOTVM_NOTHING;

      theTags(tmp) = OFILE;
      oFile(tmp) = fileinfo->file;
      res = ps_scan_field( ss, &tmp );
      if ( res != MPS_RES_OK ) return res;

      oFile(tmp) = fileinfo->orig_file;
      res = ps_scan_field( ss, &tmp );
      if ( res != MPS_RES_OK ) return res;
    }
  }
  return MPS_RES_OK;
}


/* cc_iccbased_init - initialize the ICC cache */
Bool cc_iccbased_init(void)
{
  if ( mps_root_create( &ICCCacheRoot, mm_arena, mps_rank_exact(),
                        0, scanICCCache, NULL, 0 )
       != MPS_RES_OK ) {
    HQFAIL("initICCCache: Failed to register icc cache root");
    return FAILURE(FALSE) ;
  }
  if ( !low_mem_handler_register(&gsc_profile_handler) ) {
    mps_root_destroy(ICCCacheRoot);
    return FALSE;
  }
  return TRUE;
}


/* finishICCCache - finish the ICC cache */
void cc_iccbased_finish(void)
{
  if (frontEndColorState) {
    while (frontEndColorState->ICC_cacheHead != NULL) {
      HQASSERT( frontEndColorState->ICC_cacheHead->d->devNsid <= 2,
                "ICC cache appears to contain job profiles, not just global ones");
#ifdef DEBUG_BUILD
      {
        ICC_FILELIST_INFO *listinfo = frontEndColorState->ICC_cacheHead->d->filelist_head;

        while (listinfo) {
          HQASSERT(listinfo->orig_sid <= 2,
                   "ICC cache appears to contain job profiles, not just global ones");
          HQASSERT(listinfo->orig_sid <= listinfo->sid, "Inconsistent save levels");
          listinfo=listinfo->next;
        }
      }
#endif
      profile_discard(frontEndColorState, frontEndColorState->ICC_cacheHead->d, TRUE);
    }
  }
  low_mem_handler_deregister(&gsc_profile_handler);
  if (ICCCacheRoot) {
    mps_root_destroy( ICCCacheRoot );
  }
}


/*----------------------------------------------------------------------------*/

static size_t iccbasedStructSize(void)
{
  return 0;
}

static CLINKfunctions CLINKiccbased_functions =
{
  iccbased_destroy,
  iccbased_invokeSingle,
  NULL /* iccbased_invokeBlock */,
  NULL /*iccbased_scan */
};

/*----------------------------------------------------------------------------*/

/* New AtoB constructor func */
static Bool construct_AtoB_invoke(CLINKiccbased **ppIccBasedInfo,
                                  FILELIST *flptr,
                                  int32 num_tags,
                                  icTags *tags,
                                  icTagSignature desc_sig)
{
  int8 n_curves, n_input_channels, n_output_channels;
  int32 offset;
  icTags *p;
  LUT_AB_BA_TEMP_DATA temp = {0};

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in construct_AtoB_invoke" );
  HQASSERT( flptr, "Null flptr in construct_AtoB_invoke" );
  HQASSERT( tags, "Null tags pointer in construct_AtoB_invoke" );

  if ( !read_ab_ba_lut_heading(&p, flptr, num_tags, tags, desc_sig, &temp))
    return FALSE;

  HQASSERT( temp.sig == icSiglutAtoBType, "Unexpected tag signature" );

  n_curves = n_input_channels = (*ppIccBasedInfo)->i_dimensions;
  n_output_channels = (*ppIccBasedInfo)->o_dimensions;

  /* if number of input and output channels don't match expected give up */
  if ( temp.inputChan != n_input_channels ||
       temp.outputChan != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of input or output channels in ICC profile lutAtoBType." );

  /* OK - start collecting the data */

  /* N.B. an offset of zero indicates the elements are not present */
  if ( temp.offsetAcurve != 0 ) {
    /* Read in the 'A' curves and create the mini-invokes */
    offset = temp.offsetAcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_input_channels ))
      return FALSE;
  }

  if (temp.offsetCLUT != 0) {
    /* Read in the multi-dimensional table and create the mini-invoke */
    offset = temp.offsetCLUT + p->tag.offset;
    if ( !create_clutAtoB( ppIccBasedInfo, flptr, offset, n_input_channels,
                           n_output_channels))
      return FALSE;

    n_curves = n_output_channels;
  }

  /* The number of dimensions can't change after this point */
  if ( n_curves != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of dimensions in ICC profile lutAtoBType." );

  if (temp.offsetMcurve != 0) {
    /* Read in the 'M' curves and create the mini-invokes */
    offset = temp.offsetMcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_output_channels ))
      return FALSE;

    if ( !temp.offsetMatrix ) {
      monitorf(UVS("Warning: M curves should only be used when the matrix is present.\n"));
    }
  }

  if (temp.offsetMatrix != 0) {
    if ( n_curves != 3 )
      return detail_error_handler( SYNTAXERROR,
              "Invalid number of dimensions used with matrix in ICC profile lutAtoBType." );

    /* Read in the matrix and create the mini-invoke */
    offset = temp.offsetMatrix + p->tag.offset;

    if ( !create_matrix( ppIccBasedInfo, flptr, offset ))
      return FALSE;
  }

  if ( temp.offsetBcurve != 0 ) {
    /* Read in the 'B' curves and create the mini-invokes */
    offset = temp.offsetBcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_output_channels ))
      return FALSE;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/

/* New lut8Type or lut16Type constructor func */
static Bool construct_lut8_16_invoke(CLINKiccbased **ppIccBasedInfo,
                                     FILELIST *flptr,
                                     int32 num_tags,
                                     icTags *tags,
                                     icTagSignature desc_sig)
{
  int32 offset;
  int8 n_curves, n_input_channels, n_output_channels;
  int8 precision = 1;
  icTags *p;
  LUT_TEMP_DATA       temp = {0};
  LUT_16_TEMP_DATA    temp_16lut = {0};

  HQASSERT( ppIccBasedInfo,
    "Null invoke pointer in construct_lut8_16_invoke" );
  HQASSERT( flptr, "Null flptr in construct_lut8_16_invoke" );
  HQASSERT( tags, "Null tags pointer in construct_lut8_16_invoke" );

  if (!read_lut_heading(&p, flptr, num_tags, tags, desc_sig, &temp, &temp_16lut))
    return FALSE;

  HQASSERT( ( temp.sig == icSigLut8Type ) ||
            ( temp.sig == icSigLut16Type ),
            "Unexpected tag signature" );

  if ( temp.sig == icSigLut16Type )
    precision = 2;

  n_curves = n_input_channels = (*ppIccBasedInfo)->i_dimensions;
  n_output_channels = (*ppIccBasedInfo)->o_dimensions;

  /* if number of input and output channels don't match expected give up */
  if ( temp.inputChan != n_input_channels ||
       temp.outputChan != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of input or output channels in ICC profile lut8Type or lut16Type.");

  offset = p->tag.offset + sizeof( LUT_TEMP_DATA );
  offset += ( temp.sig == icSigLut16Type ) ? sizeof( LUT_16_TEMP_DATA ) : 0;

  /* OK - start collecting the data */
  if ( !create_3by3_matrix( ppIccBasedInfo, &temp ))
    return FALSE;

  if ( temp_16lut.inputEnt > 0 ) {
    if ( temp_16lut.inputEnt == 1 )
      return detail_error_handler( SYNTAXERROR,
              "Too few input table entries in ICC profile lut16Type." );

    /* Read in the input tables and create the mini-invoke */
    if ( !create_1D_tables( ppIccBasedInfo, flptr, offset, n_input_channels,
                            temp_16lut.inputEnt, precision ))
      return FALSE;

    offset += n_input_channels * temp_16lut.inputEnt * precision;
  }

  if ( temp.clutPoints > 0 ) {
    if ( temp.clutPoints == 1 )
      return detail_error_handler( SYNTAXERROR,
              "Too few CLUT grid points in ICC profile lut8Type or lut16Type." );

    /* Read in the CLUT and create the mini-invoke */
    if ( !create_clut8_16( ppIccBasedInfo, flptr, offset, n_input_channels,
                           n_output_channels, temp.clutPoints, precision ))
      return FALSE;

    offset += (int32) pow( temp.clutPoints, n_input_channels) *
              n_output_channels * precision;
    n_curves = n_output_channels;
  }

  /* The number of dimensions can't change after this point */
  if ( n_curves != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of dimensions in ICC profile lut8Type or lut16Type." );

  if ( temp_16lut.outputEnt > 0 ) {
    if ( temp_16lut.outputEnt == 1 )
      return detail_error_handler( SYNTAXERROR,
              "Too few output table entries in ICC profile lut8Type." );

    /* Read in the output tables and create the mini-invoke */
    if ( !create_1D_tables( ppIccBasedInfo, flptr, offset, n_output_channels,
                            temp_16lut.outputEnt, precision ))
      return FALSE;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Constructor func for 3 component matrix TRC profiles */
static Bool construct_rgb_trc_invoke(CLINKiccbased **ppIccBasedInfo,
                                     FILELIST *flptr,
                                     int32 num_tags,
                                     icTags *tags,
                                     icTagSignature desc_sig,
                                     Bool isOutput)
{
  int32 i,j;
  icTagTypeSignature curve_type = 0;
  MI_PIECEWISE_LINEAR *piecewise_linear = NULL;
  MI_PARAMETRIC *parametric = NULL;
  MI_INVERSE_PARAMETRIC *inverse_parametric = NULL;
  MI_MATRIX *matrix_ptr = NULL;
  DATA_PIECEWISE_LINEAR *current_curve, *next_curve;
  MINI_INVOKE mi_linear = mi_piecewise_linear;

  uint32 piecewise_channelmask = 0;
  uint32 parametric_channelmask = 0;
  size_t piecewise_size = 0;
  size_t parametric_size = 0;
  size_t inverse_parametric_size = 0;
  uint32 n_paras = 0;

  SYSTEMVALUE temp_matrix[ 9 ], temp_inverse_matrix[ 9 ];
  uint32 curve_offset[ 3 ];
  int32 curve_length[ 3 ];
  int32 curve_types[ 3 ];
  int32 curve_tags[3] = {icSigRedTRCTag, icSigGreenTRCTag, icSigBlueTRCTag };

  icXYZType XYZblue;
  icXYZType XYZgreen;
  icXYZType XYZred;

  UNUSED_PARAM( icTagSignature, desc_sig );

  HQASSERT( ppIccBasedInfo, "Null invoke pointer" );
  HQASSERT( flptr, "Null file pointer" );
  HQASSERT( tags, "Null tags" );
  HQASSERT( !desc_sig, "Unexpected tag signature" );

  /* 3 component matrix profiles are only intended for the CIEXYZ PCS */
  if ( (*ppIccBasedInfo)->profile->abortOnBadICCProfile &&
       (*ppIccBasedInfo)->profile->pcsspace != SPACE_ICCXYZ )
    return detail_error_handler( SYNTAXERROR,
            "Unexpected PCS for 3 component matrix-based ICC profile." );

  /* Assemble the info for the matrix invoke */
  if ( !read_XYZPoint(flptr, num_tags, tags, icSigRedColorantTag, &XYZred)
    || !read_XYZPoint(flptr, num_tags, tags, icSigGreenColorantTag, &XYZgreen)
    || !read_XYZPoint(flptr, num_tags, tags, icSigBlueColorantTag, &XYZblue))
    goto tidyup;

  /* Try to allocate it */
  matrix_ptr = new_matrix(FALSE); /* don't clip the output */
  if ( !matrix_ptr ) {
    (void) error_handler(VMERROR);
    goto tidyup;
  }

  /* Scale the matrix values */
  matrix_ptr->matrix[0][0] = XYZred.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][0] = XYZred.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][0] = XYZred.data.data[0].Z / 65536.0f;

  matrix_ptr->matrix[0][1] = XYZgreen.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][1] = XYZgreen.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][1] = XYZgreen.data.data[0].Z / 65536.0f;

  matrix_ptr->matrix[0][2] = XYZblue.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][2] = XYZblue.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][2] = XYZblue.data.data[0].Z / 65536.0f;


  /* Scale the values up to the CIEXYZ range */
  for ( i = 0; i < 3; i++ ) {
    for ( j = 0; j < 3; j++ ) {
      matrix_ptr->matrix[i][j] *= (*ppIccBasedInfo)->profile->relative_whitepoint[i] /
                                  (*ppIccBasedInfo)->profile->whitepoint[i];
     }
  }

  if ( isOutput ) {
    /* Invert the TRCs */
    mi_linear = mi_inverse_linear;

    /* Invert the matrix */
    for ( i = 0; i < 3; i++ ) {
      for ( j = 0; j < 3; j++ ) {
        temp_matrix[(3 * i) + j] = matrix_ptr->matrix[i][j];
      }
    }

    if ( ! matrix_inverse_3x3( temp_matrix, temp_inverse_matrix )) {
      (void) detail_error_handler( SYNTAXERROR,
              "Unable to invert ICC profile matrix." );
      goto tidyup;
    }

    /* Fill in the mini-invoke matrix */
    matrix_ptr->clip = TRUE;  /* clip the output to 0.0f to 1.0f */

    for ( i = 0; i < 3; i++ ) {
      for ( j = 0; j < 3; j++ ) {
          matrix_ptr->matrix[i][j] = temp_inverse_matrix[(3 * i) + j];
      }
    }

    if ( !add_matrix( ppIccBasedInfo, matrix_ptr ) )
      goto tidyup;
  }

  /* First find out the curve types of all 3 curves as we want to know if they
     can be handled as piecewise linear or parametric.  Store the results as a
     bitmask.  Also need to find out how much space to allocate.
  */

  for ( i = 0; i < 3; i++ ) {
    /* Find out what type of curve it is and get the offset */
    if ( !read_trc_curve_type( flptr, curve_tags[i], num_tags, tags,
                               &curve_type, &(curve_offset[i]) ))
      return FALSE;

    if ( curve_type == icSigParametricCurveType ) {
      parametric_channelmask |= (1u<<i);
      n_paras++;

      /* Get its length and detailed type */
      if ( !get_para_info( flptr, curve_offset[i], &(curve_length[i]),
                           &(curve_types[i]) ))
        return FALSE;
    }
    else if ( curve_type == icSigCurveType ) {
      curve_types[i] = -1;

      /* Get the curve length - n.b. a curve length of 0 indicates a linear
       * curve so nothing to do here.
       */
      if ( !get_curv_length( flptr, curve_offset[i], &(curve_length[i]) ))
        return FALSE;

      if ( curve_length[i] == 1 ) {
        /* We effectively have a parametric curve after all */
        parametric_channelmask |= (1u<<i);
        n_paras++;
      }
      else if ( curve_length[i] != 0) {
        /* It's piecewise linear */
        piecewise_channelmask |= (1u<<i);
        piecewise_size += DATA_PIECEWISE_LINEAR_SIZE( curve_length[i] );
      }
    }
    else {
      /* It's an invalid curve type */
      return detail_error_handler( SYNTAXERROR,
              "Invalid curve type in ICC profile." );
    }
  }


  /* Should now know which curves are which and how much space to allocate */
  if ( piecewise_size > 0 ) {
    /* Add on space for the channelmask itself */
    piecewise_size += MI_PIECEWISE_LINEAR_SIZE;

    /* Try to allocate it */
    piecewise_linear = mi_alloc( piecewise_size );
    if ( piecewise_linear == NULL )
      return FALSE;

    /* Start filling in the structure to pass to the mini-invoke creator */
    piecewise_linear->channelmask = piecewise_channelmask;

    /* Initialise the curve pointers */
    current_curve = &(piecewise_linear->curves[0]);
    next_curve = current_curve;

    /* Fill in the data for each DATA_PIECEWISE_LINEAR */
    for ( i = 0; i < 3 ; i++ ) {
      if ( (piecewise_channelmask & (1u<<i)) != 0 ) {
        current_curve = next_curve;
        current_curve->maxindex = curve_length[i] - 1;

        if ( !read_curv_data( isOutput, flptr,
                              ( curve_offset[i] + curv_header_size ),
                              curve_length[i], current_curve ))
          goto tidyup;

        next_curve = current_curve->next = (DATA_PIECEWISE_LINEAR*) ((int8*)
          current_curve + DATA_PIECEWISE_LINEAR_SIZE(curve_length[i]));
      }
    }

    /* Set the last pointer to Null */
    current_curve->next = 0;

    /* Add this to the master invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_linear, piecewise_linear ))
      goto tidyup;
  }

  if ( n_paras > 0 ) {
    if ( !isOutput ) {
      /* Work out how much space is required */
      parametric_size += MI_PARAMETRIC_SIZE(n_paras);

      /* Try to allocate it */
      parametric = mi_alloc( parametric_size );
      if ( parametric == NULL )
        goto tidyup;

      /* Start filling in the structure to pass to the mini-invoke creator */
      parametric->channelmask = parametric_channelmask;

      /* Fill in the data for each DATA_PARAMETRIC */
      j = 0;

      for ( i = 0; i < 3 ; i++ ) {
        if ( (parametric_channelmask & (1u<<i)) != 0 ) {
          if ( !read_para_data( flptr, ( curve_offset[i] + curv_header_size),
                                curve_length[i], curve_types[i],
                                &(parametric->curves[j])))
            goto tidyup;

          j++;
        }
      }

      /* Add this to the master invoke */
      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_parametric, parametric ))
        goto tidyup;
    }
    else {
      /* isOutput */
      /* Work out how much space is required */
      inverse_parametric_size += MI_INVERSE_PARAMETRIC_SIZE(n_paras);

      /* Try to allocate it */
      inverse_parametric = mi_alloc( inverse_parametric_size );
      if ( inverse_parametric == NULL )
        goto tidyup;

      /* Start filling in the structure to pass to the mini-invoke creator */
      inverse_parametric->channelmask = parametric_channelmask;

      /* Fill in the data for each DATA_PARAMETRIC */
      j = 0;

      for ( i = 0; i < 3 ; i++ ) {
        if ( (parametric_channelmask & (1u<<i)) != 0 ) {
          if ( !read_inverse_para_data( flptr,
                                        ( curve_offset[i] + curv_header_size),
                                        curve_length[i], curve_types[i],
                                        &(inverse_parametric->curves[j])))
            goto tidyup;

          j++;
        }
      }

      /* Add this to the master invoke */
      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_inverse_parametric,
                                inverse_parametric ))
        goto tidyup;
    }
  }

  if ( !isOutput ) {
    /* For input transforms the matrix invoke follows the TRCs */
    if ( !add_matrix( ppIccBasedInfo, matrix_ptr ) )
      goto tidyup;
  }

  return TRUE;

tidyup:
  mi_free(piecewise_linear);
  mi_free(parametric);
  mi_free(inverse_parametric);
  mi_free(matrix_ptr);
  return FALSE;
}


/* Constructor func for 3 component matrix only profile for scRGB.
 * For scRGB we have created a matrixTRC style profile, but containing only a
 * matrix, no TRCs, (scRGB has a linear gamma).  Having this special style of
 * profile (which would otherwise be invalid), allows us to recognise not to
 * clip the input values to the link when used as an input, (we wouldn't anyway
 * when used as an output).
 */
static Bool construct_rgb_matrix_invoke(CLINKiccbased **ppIccBasedInfo,
                                        FILELIST *flptr,
                                        int32 num_tags,
                                        icTags *tags,
                                        icTagSignature desc_sig,
                                        Bool isOutput)
{
  int32 i,j;
  MI_MATRIX *matrix_ptr = NULL;

  SYSTEMVALUE temp_matrix[ 9 ], temp_inverse_matrix[ 9 ];

  icXYZType XYZblue;
  icXYZType XYZgreen;
  icXYZType XYZred;

  UNUSED_PARAM( icTagSignature, desc_sig );

  HQASSERT( ppIccBasedInfo, "Null invoke pointer" );
  HQASSERT( flptr, "Null file pointer" );
  HQASSERT( tags, "Null tags" );
  HQASSERT( !desc_sig, "Unexpected tag signature" );

  /* 3 component matrix profiles are only intended for the CIEXYZ PCS */
  if ( (*ppIccBasedInfo)->profile->abortOnBadICCProfile &&
       (*ppIccBasedInfo)->profile->pcsspace != SPACE_ICCXYZ )
    return detail_error_handler( SYNTAXERROR,
            "Unexpected PCS for 3 component matrix-based ICC profile." );

  /* Assemble the info for the matrix invoke */
  if ( !read_XYZPoint(flptr, num_tags, tags, icSigRedColorantTag, &XYZred)
    || !read_XYZPoint(flptr, num_tags, tags, icSigGreenColorantTag, &XYZgreen)
    || !read_XYZPoint(flptr, num_tags, tags, icSigBlueColorantTag, &XYZblue))
    goto tidyup;

  /* Try to allocate it */
  matrix_ptr = new_matrix(FALSE); /* don't clip the output */
  if ( matrix_ptr == NULL )
    goto tidyup;

  /* Scale the matrix values */
  matrix_ptr->matrix[0][0] = XYZred.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][0] = XYZred.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][0] = XYZred.data.data[0].Z / 65536.0f;

  matrix_ptr->matrix[0][1] = XYZgreen.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][1] = XYZgreen.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][1] = XYZgreen.data.data[0].Z / 65536.0f;

  matrix_ptr->matrix[0][2] = XYZblue.data.data[0].X / 65536.0f;
  matrix_ptr->matrix[1][2] = XYZblue.data.data[0].Y / 65536.0f;
  matrix_ptr->matrix[2][2] = XYZblue.data.data[0].Z / 65536.0f;


  /* Scale the values up to the CIEXYZ range */
  for ( i = 0; i < 3; i++ ) {
    for ( j = 0; j < 3; j++ ) {
      matrix_ptr->matrix[i][j] *= (*ppIccBasedInfo)->profile->relative_whitepoint[i] /
                                  (*ppIccBasedInfo)->profile->whitepoint[i];
     }
  }

  if ( isOutput ) {
    /* Invert the matrix */
    for ( i = 0; i < 3; i++ ) {
      for ( j = 0; j < 3; j++ ) {
        temp_matrix[(3 * i) + j] = matrix_ptr->matrix[i][j];
      }
    }

    if ( ! matrix_inverse_3x3( temp_matrix, temp_inverse_matrix )) {
      (void) detail_error_handler( SYNTAXERROR,
              "Unable to invert ICC profile matrix." );
      goto tidyup;
    }

    /* Fill in the mini-invoke matrix */
    matrix_ptr->clip = TRUE;  /* clip the output to 0.0f to 1.0f */

    for ( i = 0; i < 3; i++ ) {
      for ( j = 0; j < 3; j++ ) {
          matrix_ptr->matrix[i][j] = temp_inverse_matrix[(3 * i) + j];
      }
    }

    if ( !add_matrix( ppIccBasedInfo, matrix_ptr ) )
      goto tidyup;
  }
  else {
    /* For input transforms simply add the matrix */
    if ( !add_matrix( ppIccBasedInfo, matrix_ptr ) )
      goto tidyup;
  }

  return TRUE;

tidyup:
  mi_free(matrix_ptr);
  return FALSE;
}


/*----------------------------------------------------------------------------*/
/* Constructor func for 3 component matrix TRC input profiles */
static Bool construct_rgb_trc_input_invoke(CLINKiccbased **ppIccBasedInfo,
                                           FILELIST *flptr,
                                           int32 num_tags,
                                           icTags *tags,
                                           icTagSignature desc_sig)
{
  return construct_rgb_trc_invoke(ppIccBasedInfo,
                                  flptr,
                                  num_tags,
                                  tags,
                                  desc_sig,
                                  FALSE);
}


/* Constructor func for 3 component matrix TRC output profiles */
static Bool construct_rgb_trc_output_invoke(CLINKiccbased **ppIccBasedInfo,
                                            FILELIST *flptr,
                                            int32 num_tags,
                                            icTags *tags,
                                            icTagSignature desc_sig)
{
  return construct_rgb_trc_invoke(ppIccBasedInfo,
                                  flptr,
                                  num_tags,
                                  tags,
                                  desc_sig,
                                  TRUE);
}


/* Constructor func for 3 component matrix only input profile for scRGB */
static Bool construct_rgb_matrix_input_invoke(CLINKiccbased **ppIccBasedInfo,
                                              FILELIST *flptr,
                                              int32 num_tags,
                                              icTags *tags,
                                              icTagSignature desc_sig)
{
  return construct_rgb_matrix_invoke(ppIccBasedInfo,
                                     flptr,
                                     num_tags,
                                     tags,
                                     desc_sig,
                                     FALSE);
}


/* Constructor func for 3 component matrix only output profile for scRGB */
static Bool construct_rgb_matrix_output_invoke(CLINKiccbased **ppIccBasedInfo,
                                               FILELIST *flptr,
                                               int32 num_tags,
                                               icTags *tags,
                                               icTagSignature desc_sig)
{
  return construct_rgb_matrix_invoke(ppIccBasedInfo,
                                     flptr,
                                     num_tags,
                                     tags,
                                     desc_sig,
                                     TRUE);
}

/*----------------------------------------------------------------------------*/
/* Constructor func for Monochrome profiles */
static Bool construct_gray_trc_invoke(CLINKiccbased **ppIccBasedInfo,
                                      FILELIST *flptr,
                                      int32 num_tags,
                                      icTags *tags,
                                      icTagSignature desc_sig,
                                      Bool isOutput)
{
  icTagTypeSignature    curve_type = 0;  /* curveType or parametricCurveType */
  MI_PIECEWISE_LINEAR   *piecewise_linear = NULL;
  MI_PARAMETRIC         *parametric = NULL;
  MI_INVERSE_PARAMETRIC *inverse_parametric = NULL;
  MI_MULTIPLY           *multiply = NULL;
  DATA_PIECEWISE_LINEAR *current_curve;
  MINI_INVOKE           mi_linear;

  size_t piecewise_size = 0;
  size_t parametric_size = 0;
  size_t inverse_parametric_size = 0;
  uint32 curve_offset = 0;
  int32  curve_length = 0;
  int32  n_paras = 0;
  int32  sub_type;
  int    i;
  uint32 mask;

  UNUSED_PARAM( icTagSignature, desc_sig );

  HQASSERT( ppIccBasedInfo, "Null invoke pointer" );
  HQASSERT( flptr, "Null file pointer" );
  HQASSERT( tags, "Null tags" );
  HQASSERT( desc_sig, "No tag signature" );

  /* Gray TRC profiles only require one channel for input or output depending
   * on the direction. This will be either the L or Y channel depending on the
   * PCS flavour, which is controlled by a mask value.
   */
  if ( isOutput ) {
    mi_linear = mi_inverse_linear;
    if ((*ppIccBasedInfo)->iColorSpace == SPACE_ICCLab)
      mask = 0x1;
    else {
      HQASSERT((*ppIccBasedInfo)->iColorSpace == SPACE_ICCXYZ,
               "Unexpected color space");
      mask = 0x2;
    }
  }
  else {
    mi_linear = mi_piecewise_linear;
    mask = 0x1;     /* There is only one input colorant in the first colorant slot */
  }

  /* Find out what type of curve it is and get the offset */
  if ( !read_trc_curve_type( flptr, desc_sig, num_tags, tags, &curve_type, &curve_offset ))
    return FALSE;

  if ( curve_type == icSigParametricCurveType ) {
    n_paras++;

    /* Get its length (and detailed type) */
    if ( !get_para_info( flptr, curve_offset, &curve_length, &sub_type))
      return FALSE;
  }
  else if ( curve_type == icSigCurveType ) {
    sub_type = -1;

    /* Get the curve length - n.b. a curve length of 0 indicates a linear
     * curve so nothing to do here.
     */
    if ( !get_curv_length( flptr, curve_offset, &curve_length))
      return FALSE;

    if ( curve_length == 1 ) {
      /* We effectively have a parametric curve after all */
      n_paras++;
    }
    else if ( curve_length != 0) {
      /* It's piecewise linear */
      piecewise_size = DATA_PIECEWISE_LINEAR_SIZE( curve_length );
    }
  }
  else {
    /* It's an invalid curve type */
    return detail_error_handler( SYNTAXERROR,
            "Invalid curve type in ICC profile." );
  }

  /* Should now know what type of curve and how much space to allocate */
  if ( piecewise_size > 0 ) {
    /* Add on space for the channelmask itself */
    piecewise_size += MI_PIECEWISE_LINEAR_SIZE;

    /* Try to allocate it */
    piecewise_linear = mi_alloc( piecewise_size );
    if ( piecewise_linear == NULL )
      return FALSE;

    /* Start filling in the structure to pass to the mini-invoke creator */
    piecewise_linear->channelmask = mask;

    /* Initialise the curve pointer */
    current_curve = &(piecewise_linear->curves[0]);

    /* Fill in the data for the DATA_PIECEWISE_LINEAR */
    current_curve->maxindex = curve_length - 1;

    if ( !read_curv_data( isOutput, flptr, ( curve_offset + curv_header_size ),
                          curve_length, current_curve ))
      goto tidyup;

    /* Set the last pointer to Null */
    current_curve->next = 0;

    /* Add this to the master invoke */
    if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_linear, piecewise_linear ))
      goto tidyup;
  }
  else if ( n_paras > 0 ) {
    HQASSERT( n_paras == 1, "Too many curves in construct_gray_trc_invoke" );

    if ( !isOutput ) {
      /* Work out how much space is required */
      parametric_size = MI_PARAMETRIC_SIZE(n_paras);

      /* Try to allocate it */
      parametric = mi_alloc( parametric_size );
      if ( parametric == NULL )
        return FALSE;

      /* Start filling in the structure to pass to the mini-invoke creator */
      parametric->channelmask = mask;

      /* Initialise the curve pointer */
      if ( !read_para_data( flptr, (curve_offset + curv_header_size),
                            curve_length, sub_type, &(parametric->curves[0])))
        goto tidyup;

      /* Add this to the master invoke */
      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_parametric, parametric ))
        goto tidyup;
    }
    else {
      /* isOutput */
      inverse_parametric_size = MI_INVERSE_PARAMETRIC_SIZE(n_paras);

      /* Try to allocate it */
      inverse_parametric = mi_alloc( inverse_parametric_size );
      if ( inverse_parametric == NULL )
        return FALSE;

      /* Start filling in the structure to pass to the mini-invoke creator */
      inverse_parametric->channelmask = mask;

      /* Initialise the curve pointer */
      if ( !read_inverse_para_data( flptr, (curve_offset + curv_header_size),
                                    curve_length, sub_type,
                                    &(inverse_parametric->curves[0])))
        goto tidyup;

      /* Add this to the master invoke */
      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_inverse_parametric,
                                inverse_parametric ))
        goto tidyup;
    }
  }

  if ( !isOutput ) {
    if ((*ppIccBasedInfo)->oColorSpace == SPACE_ICCLab) {
      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_neutral_ab, NULL ))
        goto tidyup;
    }
    else {
      /* now convert to XYZ by multiplying by the whitepoint */

      HQASSERT((*ppIccBasedInfo)->oColorSpace == SPACE_ICCXYZ,
               "Unexpected color space");
      /* Try to allocate the scaling invoke */
      multiply = mi_alloc( sizeof(MI_MULTIPLY) );
      if ( multiply == NULL )
        goto tidyup;

      for (i=0; i<3; i++)
        multiply->color[i] = (*ppIccBasedInfo)->profile->relative_whitepoint[i];

      if ( !mi_add_mini_invoke( ppIccBasedInfo, mi_multiply, multiply ))
        goto tidyup;
    }
  }

  /* done */
  return TRUE;

tidyup:
  mi_free(piecewise_linear);
  mi_free(parametric);
  mi_free(inverse_parametric);
  mi_free(multiply);
  return FALSE;
}


/* Constructor func for Monochrome input profiles */
static Bool construct_gray_trc_input_invoke(CLINKiccbased **ppIccBasedInfo,
                                            FILELIST *flptr,
                                            int32 num_tags,
                                            icTags *tags,
                                            icTagSignature desc_sig)
{
  return construct_gray_trc_invoke(ppIccBasedInfo,
                                   flptr,
                                   num_tags,
                                   tags,
                                   desc_sig,
                                   FALSE);
}


/* Constructor func for Monochrome output profiles */
static Bool construct_gray_trc_output_invoke(CLINKiccbased **ppIccBasedInfo,
                                             FILELIST *flptr,
                                             int32 num_tags,
                                             icTags *tags,
                                             icTagSignature desc_sig)
{
  return construct_gray_trc_invoke(ppIccBasedInfo,
                                   flptr,
                                   num_tags,
                                   tags,
                                   desc_sig,
                                   TRUE);
}

/*----------------------------------------------------------------------------*/
/* New BtoA constructor func */
static Bool construct_BtoA_invoke(CLINKiccbased **ppIccBasedInfo,
                                  FILELIST *flptr,
                                  int32 num_tags,
                                  icTags *tags,
                                  icTagSignature desc_sig)
{
  int8 n_curves, n_input_channels, n_output_channels;
  int32 offset;
  icTags *p;
  LUT_AB_BA_TEMP_DATA temp = {0};

  HQASSERT( ppIccBasedInfo, "Null invoke pointer in construct_BtoA_invoke" );
  HQASSERT( flptr, "Null flptr in construct_BtoA_invoke" );
  HQASSERT( tags, "Null tags pointer in construct_BtoA_invoke" );

  if ( !read_ab_ba_lut_heading(&p, flptr, num_tags, tags, desc_sig, &temp))
    return FALSE;

  HQASSERT( temp.sig == icSiglutBtoAType, "Unexpected tag signature" );

  n_curves = n_input_channels = (*ppIccBasedInfo)->i_dimensions;
  n_output_channels = (*ppIccBasedInfo)->o_dimensions;

  /* if number of input and output channels don't match expected give up */
  if ( temp.inputChan != n_input_channels ||
       temp.outputChan != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of input or output channels in ICC profile lutBtoAType.");

  /* OK - start collecting the data */

  /* N.B. an offset of zero indicates the elements are not present */
  if ( temp.offsetBcurve != 0 ) {
    /* Read in the 'B' curves and create the mini-invokes */
    offset = temp.offsetBcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_input_channels ))
      return FALSE;
  }

  if (temp.offsetMatrix != 0) {
    if ( n_input_channels != 3 )
      return detail_error_handler( SYNTAXERROR,
              "Invalid number of dimensions used with matrix in ICC profile lutBtoAType.");

    /* Read in the matrix and create the mini-invoke */
    offset = temp.offsetMatrix + p->tag.offset;

    if ( !create_matrix( ppIccBasedInfo, flptr, offset ))
      return FALSE;
  }

  if (temp.offsetMcurve != 0) {
    /* Read in the 'M' curves and create the mini-invokes */
    offset = temp.offsetMcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_input_channels ))
      return FALSE;

    if ( !temp.offsetMatrix ) {
      monitorf(UVS("Warning: M curves should only be used when the matrix is present.\n"));
    }
  }

  if (temp.offsetCLUT != 0) {
    /* Read in the multi-dimensional table and create the mini-invoke */
    offset = temp.offsetCLUT + p->tag.offset;
    if ( !create_clutAtoB( ppIccBasedInfo, flptr, offset, n_input_channels,
                           n_output_channels))
      return FALSE;

    n_curves = n_output_channels;
  }

  /* The number of dimensions can't change after this point */
  if ( n_curves != n_output_channels )
    return detail_error_handler( SYNTAXERROR,
            "Inconsistent number of dimensions in ICC profile lutBtoAType.");

  if ( temp.offsetAcurve != 0 ) {
    /* Read in the 'A' curves and create the mini-invokes */
    offset = temp.offsetAcurve + p->tag.offset;

    if ( !create_1D_curves( ppIccBasedInfo, flptr, offset, n_output_channels ))
      return FALSE;
  }


  return TRUE;
}

/*----------------------------------------------------------------------------*/

/* Find the appropriate constructor function.
 * For v4 profiles we will need to pass an intent in here.
 */
static Bool select_constructor( Bool isOutput,
                                int32 num_tags,
                                icTags *tags,
                                COLORSPACE_ID devicespace,
                                uint8 desiredIntent,
                                uint8 *intentUsed,
                                icTagSignature *desc_sig,
                                int32 *outputType,
                                int32 *inputType,
                                CONSTRUCTOR_FUNCTION *converter )
{
  int32 tagsfound = 0;
  int32 tagsneeded = 0;

  icTags *tag_to_use = NULL;

  HQASSERT( desiredIntent < N_ICC_TABLES,
    "Invalid desiredIntent");

  /* For the time being assume we are going to fill the relevant slot with info
     from tags having an icTagSignature in following order of preference:

     icSigAToB1Tag (colorimetric)
     icSigAToB0Tag (perceptual)
     icSigAToB2Tag (saturation)

     If none of the above are present, and if the colorspace in the header is
     RGB, (some CMYK profiles erroneously have TRCs too), go on to see if we
     have all of the following:

     icSigRedTRCTag
     icSigGreenTRCTag
     icSigBlueTRCTag
     icSigRedColorantTag
     icSigGreenColorantTag
     icSigBlueColorantTag

     If none of the first 3 are present but the last 3 are it must be our
     special matrix only scRGB profile.

     If we still haven't found a constructor, and if the colorspace in the
     header is Gray, (some CMYK profiles erroneously have TRCs too), go on to
     see if we have the following:

     icSigGrayTRCTag
   */

  HQASSERT( tags, "Null icTags pointer in select_constructor" );
  HQASSERT( intentUsed, "Null intentUsed in select_constructor" );
  HQASSERT( desc_sig, "Null icTagSignature in select_constructor" );
  HQASSERT( inputType, "Null inputType in select_constructor" );
  HQASSERT( outputType, "Null outputType in select_constructor" );
  HQASSERT( converter, "Null converter in select_constructor" );

  *desc_sig = 0;
  *converter = NULL;
  *intentUsed = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  note_interesting_tags(num_tags, tags, &tagsfound );

  if ( !isOutput ) {
    /* Attempt to find the table for the desired intent */
    if ((tagsfound & (sigbitAToB0Tag | sigbitAToB1Tag | sigbitAToB2Tag)) != 0 ) {
      if ( (tagsfound & sigbitAToB1Tag) != 0 &&
            desiredIntent == SW_CMM_INTENT_RELATIVE_COLORIMETRIC ) {
        *desc_sig = icSigAToB1Tag;
      }
      else if ( (tagsfound & sigbitAToB0Tag) != 0 &&
                desiredIntent == SW_CMM_INTENT_PERCEPTUAL ) {
        *intentUsed = SW_CMM_INTENT_PERCEPTUAL;
        *desc_sig = icSigAToB0Tag;
      }
      else if ( (tagsfound & sigbitAToB2Tag) != 0 &&
                desiredIntent == SW_CMM_INTENT_SATURATION ) {
        *intentUsed = SW_CMM_INTENT_SATURATION;
        *desc_sig = icSigAToB2Tag;
      }
      else {
        /* Desired table not present, pick first one found */
        if ( (tagsfound & sigbitAToB1Tag) != 0  ) {
          *desc_sig = icSigAToB1Tag;
        }
        else if ( (tagsfound & sigbitAToB0Tag) != 0 ) {
          *intentUsed = SW_CMM_INTENT_PERCEPTUAL;
          *desc_sig = icSigAToB0Tag;
        }
        else if ( (tagsfound & sigbitAToB2Tag) != 0 ) {
          *intentUsed = SW_CMM_INTENT_SATURATION;
          *desc_sig = icSigAToB2Tag;
        }
      }
      if (*desc_sig != 0)
        tag_to_use = findTag( *desc_sig, num_tags, tags );

      /* Need to find out the detailed type to work out which function to use */
      if (tag_to_use != NULL) {
        switch (tag_to_use->points_at) {
        case icSiglutAtoBType:
          *converter = construct_AtoB_invoke;
          break;

        case icSigLut16Type:
          if (*outputType == OUTPUT_LAB)  *outputType = OUTPUT_OLD_LAB;
          /* drop through into icSigLut8Type */

        case icSigLut8Type:
          *converter = construct_lut8_16_invoke;
          break;
        }
      }
    }
  }
  else {
    /* We are constructing an output link */
    if ((tagsfound & (sigbitBToA0Tag | sigbitBToA1Tag | sigbitBToA2Tag)) != 0 ) {
      /* Attempt to find the table for the desired intent */
      if ( (tagsfound & sigbitBToA1Tag) != 0 &&
           desiredIntent == SW_CMM_INTENT_RELATIVE_COLORIMETRIC ) {
        *desc_sig = icSigBToA1Tag;
      }
      else if ( (tagsfound & sigbitBToA0Tag) != 0 &&
                desiredIntent == SW_CMM_INTENT_PERCEPTUAL ) {
        *intentUsed = SW_CMM_INTENT_PERCEPTUAL;
        *desc_sig = icSigBToA0Tag;
      }
      else if ( (tagsfound & sigbitBToA2Tag) != 0 &&
                desiredIntent == SW_CMM_INTENT_SATURATION ) {
        *intentUsed = SW_CMM_INTENT_SATURATION;
        *desc_sig = icSigBToA2Tag;
      }
      else {
        /* Desired table not present, pick first one found */
        if ( (tagsfound & sigbitBToA1Tag) != 0 ) {
          *desc_sig = icSigBToA1Tag;
        }
        else if ( (tagsfound & sigbitBToA0Tag) != 0 ) {
          *intentUsed = SW_CMM_INTENT_PERCEPTUAL;
          *desc_sig = icSigBToA0Tag;
        }
        else if ( (tagsfound & sigbitBToA2Tag) != 0 ) {
          *intentUsed = SW_CMM_INTENT_SATURATION;
          *desc_sig = icSigBToA2Tag;
        }
      }
      if (*desc_sig != 0)
        tag_to_use = findTag( *desc_sig, num_tags, tags );

      /* Need to find out the detailed type to work out which function to use */
      if (tag_to_use != NULL) {
        switch (tag_to_use->points_at) {
        case icSiglutBtoAType:
          *converter = construct_BtoA_invoke;
          break;

        case icSigLut16Type:
          if (*inputType == INPUT_LAB)  *inputType = INPUT_OLD_LAB;
          /* drop through into icSigLut8Type */

        case icSigLut8Type:
          *converter = construct_lut8_16_invoke;
          break;
        }
      }
    }
  }

  if ( *converter == NULL && devicespace == SPACE_DeviceRGB ) {
    /* or has been changed to DeviceRGB from DeviceCMY */
    tagsneeded = ( sigbitRedTRCTag | sigbitGreenTRCTag | sigbitBlueTRCTag |
                   sigbitRedColorantTag | sigbitGreenColorantTag |
                   sigbitBlueColorantTag );

    if ( (tagsfound & tagsneeded) == tagsneeded ) {
      /* Treat as a 3 component matrix based display profile.
       * Always treat as CIEXYZ regardless of the device space in the header.
       * There is no need to convert colours to ICC XYZ because that is accounted
       * for in the converter.
       */
      if ( !isOutput ) {
        *converter = construct_rgb_trc_input_invoke;
        *outputType = OUTPUT_CIE_XYZ;
      }
      else {
        *converter = construct_rgb_trc_output_invoke;
        *inputType = INPUT_CIE_XYZ;
      }
    }
    else {
      tagsneeded = ( sigbitRedColorantTag | sigbitGreenColorantTag |
                     sigbitBlueColorantTag );

      if ( (tagsfound & tagsneeded) == tagsneeded ) {
        /* Presumably this is our special matrix-only profile for scRGB.
         * Always treat as CIEXYZ regardless of the device space in the header.
         * There is no need to convert colours to ICC XYZ because that is accounted
         * for in the converter.
         */
        if ( !isOutput ) {
          *converter = construct_rgb_matrix_input_invoke;
          *outputType = OUTPUT_CIE_XYZ;
        }
        else {
          *converter = construct_rgb_matrix_output_invoke;
          *inputType = INPUT_CIE_XYZ;
        }
      }
    }
  }

  if ( *converter == NULL && devicespace == SPACE_DeviceGray ) {
    if ( (tagsfound & sigbitGrayTRCTag) != 0 ) {
      *desc_sig = icSigGrayTRCTag;

      /* Treat as a gray trc profile.
       * Unlike RGB TRC profiles, gray TRCs can be either XYZ or Lab as normal.
       * If it is Lab, the L will be converted to the output gray.
       * If it is XYZ, the Y will be converted to the output gray.
       */
      if ( !isOutput ) {
        *converter = construct_gray_trc_input_invoke;
        if (*outputType == OUTPUT_XYZ)
          *outputType = OUTPUT_CIE_XYZ;
      }
      else {
        *converter = construct_gray_trc_output_invoke;
        if (*inputType == INPUT_XYZ)
          *inputType = INPUT_CIE_XYZ;
      }
    }
  }

  if ( *converter == NULL )
    return detail_error_handler( SYNTAXERROR,
            "ICC profile lacks one or more tags." );

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Calculate the MD5 for either the whole profile, or just for the profile
 * header, and fill in the appropriate value in the ICC_PROFILE_INFO.
 */
static Bool calculate_profile_MD5(ICC_PROFILE_INFO *pInfo,
                                  FILELIST *md5_file,
                                  Bool header_only)
{
  int32 bufflen, remaining, size, last;
  uint8* md5buff;

  HQASSERT(pInfo != NULL, "ICC_PROFILE_INFO is Null");
  HQASSERT(md5_file != NULL, "ICC profile FILELIST is Null");

  if ( !mi_seek(md5_file, 0) )
    return detail_error_handler( IOERROR, "Unable to seek in ICC profile" );

  if ( header_only )
    bufflen = sizeof(icHeader);
  else
    bufflen = min( MD5BUFFLEN, pInfo->profile_size );

  md5buff = mi_alloc( bufflen );
  if ( md5buff == NULL )
    return FALSE;

  /* Don't fail here if we couldn't read in as many bytes as we wanted.
   * It may be that the profile length is wrong.
   */
  if ( file_read(md5_file, (uint8*) md5buff, bufflen, &bufflen) > 0 ) {

    /* The MD5 is should be calculated after the Profile Flags (bytes 44-47),
     * rendering intent (bytes 64-67), and profileID (bytes 84-89) field have
     * been temporarily replaced by zeroes.  (Since we calculate the header MD5
     * regardless of whether there is a profileID for the whole profile, this
     * value cannot be assumed to be zero).
     */
    ZERO(&md5buff[44], 4 * sizeof(int8));
    ZERO(&md5buff[64], 4 * sizeof(int8));
    ZERO(&md5buff[84], 4 * sizeof(int8));

    if ( header_only ) {
      md5( md5buff, bufflen, &pInfo->header_md5[0]);
      pInfo->validHeaderMD5 = TRUE;
    }
    else {
      remaining = pInfo->profile_size - bufflen;

      md5_progressive( md5buff, bufflen, &(pInfo->md5[0]),
                       pInfo->profile_size, TRUE, (remaining == 0));

      while ( remaining ) {
        size = min( MD5BUFFLEN, remaining );
        last = (size == remaining);

        if ( file_read(md5_file, (uint8*) md5buff, size, NULL) <= 0 ) {
          ZERO(&(pInfo->md5[0]), MD5_OUTPUT_LEN);
          break;
        }

        md5_progressive( md5buff, size, &(pInfo->md5[0]),
                         pInfo->profile_size, FALSE, last);

        remaining -= size;
      }

      if ( !remaining )
        pInfo->validMD5 = TRUE;
    }
  }
  mi_free( md5buff );

  return TRUE;
}


/* Calculate the MD5 for either the whole profile, or just for the profile
 * header, and fill in the appropriate value in the ICC_PROFILE_INFO.
 * Where possible this uses the underlying file rather than the RSD to
 * do the calculation.
 *
 * N.B. It is up to the caller to test for a validHeaderMD5 or validMD5
 *      flag as required, as we do not fail for what may simply be an
 *      invalid profile length in the header.
 */
static Bool calculate_profile_MD5_from_profile_info(ICC_PROFILE_INFO *pInfo,
                                                    Bool header_only)
{
  Bool use_orig_file = FALSE;
  FILELIST *orig_file, *md5_file;
  Bool real_rewindable;
  Hq32x2 file_offset;
  Bool success;

  HQASSERT(pInfo != NULL, "Null pInfo in calculate_profile_MD5_from_profile_info");
  HQASSERT(!pInfo->validMD5, "Profile MD5 is valid already");

  /* N.B. A profile with no ICC_FILELIST_INFO would not be useless if it had
   *      a validMD5, but we are here because it doesn't.
   */
  HQASSERT(pInfo->filelist_head != NULL, "Useless pInfo has not been discarded");

  /* This can happen if the ICC_FILELIST_INFO has a valid UniqueID */
  if (pInfo->filelist_head->orig_file == NULL)
    return TRUE ;

  orig_file = pInfo->filelist_head->orig_file;
  real_rewindable = !isIFilter(orig_file) && isIRewindable(orig_file);

  md5_file = pInfo->filelist_head->file;

  if (real_rewindable) {
    if ((*theIMyFilePos(orig_file))(orig_file, &file_offset) == EOF ||
        (*theIMyResetFile(orig_file))(orig_file) == EOF ) {
      (void) (*theIFileLastError(orig_file))(orig_file);
      error_clear();
    }
    else {
      md5_file = orig_file;
      use_orig_file = TRUE;
    }
  }

  if (!use_orig_file) {
    if (!set_icc_file( pInfo->filelist_head, pInfo->filelist_head->file)) {
      error_clear();
      return TRUE;
    }
    md5_file = pInfo->filelist_head->file;
  }

  success = calculate_profile_MD5(pInfo, md5_file, header_only);

  if (use_orig_file) {
    if ((*theIMyResetFile(md5_file))(md5_file) == EOF ||
        (*theIMySetFilePos(md5_file))(md5_file,&file_offset) == EOF) {
      (void) (*theIFileLastError(md5_file))(md5_file);
      error_clear();
    }
  }

  return success;
}
/*----------------------------------------------------------------------------*/
/* Find or make an icc_profile_info_cache element */
static ICC_PROFILE_INFO *get_cached_icc_info( COLOR_STATE     *colorState,
                                              OBJECT          *iccFileObj,
                                              ICC_PROFILE_ID  *uniqueID )
{
  corecontext_t *context = get_core_context_interp();
  ICC_PROFILE_INFO *pInfo = NULL;
  ICC_PROFILE_INFO_CACHE *cacheItem, *previous = NULL;
  ICC_PROFILE_INFO *cached = NULL;
  ICC_FILELIST_INFO *listinfo = NULL;
  FILELIST *md5_file;
  FILELIST *flptr;
  int32 i;
  int32 num_tags = 0;
  icTags *tags = NULL;
  Bool rewindable = FALSE;
  Bool match_found_on_MD5 = FALSE;
  ICC_PROFILE_INFO_CACHE *cacheHead = colorState->ICC_cacheHead;

  /* First see if we already have one that matches filelist or uniqueID */
  pInfo = find_cached_icc_info( colorState, iccFileObj, uniqueID );
  if ( pInfo != NULL )
    return pInfo;

  /* Make one with a single ICC_FILELIST_INFO to fill in from the header */
  pInfo = create_icc_info();

  if ( pInfo == NULL )
    return NULL;

  /* Start to fill in the structures */
  pInfo->refCnt = 1;
  pInfo->validMD5 = FALSE;
  pInfo->validHeaderMD5 = FALSE;
  pInfo->validProfile = FALSE;
  pInfo->abortOnBadICCProfile = context->page->colorPageParams.abortOnBadICCProfile;
  pInfo->useAlternateSpace = FALSE;
  pInfo->inputTablePresent = FALSE;
  pInfo->outputTablePresent = FALSE;
  pInfo->devicelinkTablePresent = FALSE;
  pInfo->is_scRGB = FALSE;

  pInfo->preferredIntent = INVALID_INTENT;    /* assume an out of range value */
  pInfo->devNsid = -1;
  pInfo->dev_DeviceNobj = onull; /* Struct copy to set slot properties */
  pInfo->pcs_DeviceNobj = onull; /* Struct copy to set slot properties */

  listinfo = pInfo->filelist_head;
  listinfo->uniqueID = *uniqueID;

  flptr = fileObjToflptr(iccFileObj);
  if (flptr == NULL)
    goto tidyup;
  if (!initialise_icc_file(listinfo, flptr))
    goto tidyup;

  rewindable = isIRewindable(listinfo->orig_file);

  /* If the original file is rewindable calculate any MD5 before adding
   * an RSD.
   */
  if (rewindable) {

    /* Have a look at the profile */
    if (!query_profile_header( pInfo )) {
      if (!add_icc_info_to_cache( colorState, pInfo, TRUE ))    /* so we don't try to set_icc_file again */
        return NULL;
      return pInfo;
    }

    if (!mi_seek(flptr, 0))
      goto tidyup;

    md5_file = listinfo->orig_file;
  }
  else {

    /* Set the file and original filelists and have a look at the profile */
    if ( !set_icc_file( listinfo, flptr ))
      goto tidyup;

    if (!query_profile_header( pInfo )) {
      if (!add_icc_info_to_cache( colorState, pInfo, TRUE ))    /* so we don't try to set_icc_file again */
        return NULL;
      return pInfo;
    }

    md5_file = listinfo->file;
  }

  /* Looks like we do really have an ICC profile */
  pInfo->validProfile = TRUE;

  /* Calculate an MD5 for the profile header regardless of whether one exists
   * for the whole profile, as we may need to check another profile header MD5
   * against it later on.  This is a performance enhancement to avoid
   * unneccessarily calculating an MD5 for the whole profile.
   */
  if (!calculate_profile_MD5(pInfo, md5_file, TRUE))
    goto tidyup;

  /* Also see if we have a non-zero MD5 for the whole profile. */
  for ( i=0; i< MD5_OUTPUT_LEN; i++ ) {
    if ( pInfo->md5[i] != 0 ) {
      pInfo->validMD5 = TRUE;
      break;
    }
  }

  /* Check whether there is an existing ICC_PROFILE_INFO with the
   * same MD5, if so the listinfo created above can be attached to it, and
   * we can free the ICC_PROFILE_INFO created above.  We first check
   * the MD5 of the header, and only if it matches do we check the whole
   * profile.
   */
  for ( cacheItem = cacheHead; cacheItem != NULL; cacheItem = cacheItem->next ) {
      cached = cacheItem->d;

    if ( cached->validHeaderMD5 ) {
      if ( HqMemCmp( &(pInfo->header_md5[0]), MD5_OUTPUT_LEN,
                     &(cached->header_md5[0]), MD5_OUTPUT_LEN) == 0 ) {

        /* Headers match, so see if the whole profile does */
        if (!pInfo->validMD5 && !calculate_profile_MD5(pInfo, md5_file, FALSE))
          goto tidyup;

        if (pInfo->validMD5) {
          if (!cached->validMD5) {
            /* Calculate the MD5 of the cached profile using the underlying
             * file if possible.
             */
            if (!calculate_profile_MD5_from_profile_info(cached, FALSE))
              goto tidyup;
          }

          if (cached->validMD5) {
            if (HqMemCmp( &(pInfo->md5[0]), MD5_OUTPUT_LEN,
                          &(cached->md5[0]), MD5_OUTPUT_LEN) == 0 ) {

              if ( previous ) {
                /* promote the ICC_PROFILE_INFO_CACHE */
                previous->next = cacheItem->next;
                cacheItem->next = colorState->ICC_cacheHead;
                colorState->ICC_cacheHead = cacheItem;
              }

              /* Attach the new ICC_FILELIST_INFO at the end of the open ones */
              insert_listinfo( cached, listinfo, 0);

              /* Free the ICC_PROFILE_INFO created above */
              mi_free( pInfo );

              match_found_on_MD5 = TRUE;
              break;
            }
          }
        }
      }
    }
    previous = cacheItem;
  }

  /* Now add the RSD (if necessary) if we didn't do so earlier */
  if (rewindable) {
    if (!mi_seek(flptr, 0) || !set_icc_file( listinfo, flptr )) {
      if (!match_found_on_MD5)
        goto tidyup;
      else
        return NULL;
    }
  }

  if (match_found_on_MD5)
    return cached;

  /* We now know what kind of profile the header claims it is. Now find out if
   * it contains the appropriate tables and/or trc's for both input and output.
   * The most maintainable way to do this is to do a dummy run through
   * select_constructor().
   */
  {
    /* These assignments aren't critical, but do have to be within range */
    int32 dummy_inputType = INPUT_OTHER;
    int32 dummy_outputType = OUTPUT_OTHER;
    uint8 dummy_desiredIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    uint8 dummy_intentUsed = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
    icTagSignature dummy_desc_sig;
    /* These are the only things returned that we're interested in */
    CONSTRUCTOR_FUNCTION input_constructor_func;
    CONSTRUCTOR_FUNCTION output_constructor_func;

    /* Now get more info from the profile */
    if ( !readTags( pInfo->filelist_head->file, &num_tags, &tags ))
      goto tidyup;

    /* First see if an input table is present */
    if ( !select_constructor( FALSE, num_tags, tags, pInfo->devicespace,
                              dummy_desiredIntent,
                              &dummy_intentUsed,
                              &dummy_desc_sig,
                              &dummy_outputType, &dummy_inputType,
                              &input_constructor_func )) {
      /* The function failed, but we don't need this table except for blend
       * spaces and emulation.
       */
      error_clear();
    }
    if (input_constructor_func != NULL) {
      if (pInfo->deviceClass == icSigLinkClass)
        pInfo->devicelinkTablePresent = TRUE;
      else
        pInfo->inputTablePresent = TRUE;

      if (input_constructor_func == construct_rgb_matrix_input_invoke) {
        /* Assume this must be our special matrix only scRGB profile */
        pInfo->is_scRGB = TRUE;
      }
    }

    /* Now see if an output table is present - n.b. we don't currently handle
     * Lab colorconversion profiles as output profiles.
     */
    if (pInfo->deviceClass != icSigLinkClass && pInfo->devicespace != SPACE_Lab) {
      if ( !select_constructor( TRUE, num_tags, tags, pInfo->devicespace,
                                dummy_desiredIntent,
                                &dummy_intentUsed,
                                &dummy_desc_sig,
                                &dummy_outputType, &dummy_inputType,
                                &output_constructor_func )) {
        /* The function failed, but we don't need this table except for blend
         * spaces and emulation.
         */
        error_clear();
      }
      if (output_constructor_func != NULL) {
        pInfo->outputTablePresent = TRUE;
      }
    }

    mi_free(tags);
  }

  /* We didn't find a match so attach the ICC_PROFILE_INFO created above
   * to the cache. NB. The refCnt is already 1 assuming it will be referenced
   * from this cache.
   */
  if (!add_icc_info_to_cache( colorState, pInfo, FALSE ))
    return NULL;

  return pInfo;

tidyup:
  profile_discard(colorState, pInfo, FALSE);
  return NULL;
}


/*----------------------------------------------------------------------------*/
/* Find or make a CLINK with its CLINKiccbased - a CLINKiccbased* of -1
 * indicates a duff profile.
 */
static CLINKiccbased* get_iccbasedLink( ICC_PROFILE_INFO *pInfo,
                                        Bool isOutput,
                                        uint8 desiredIntent )
{
  int32 num_tags = 0;
  icTags *tags = NULL;
  size_t structSize;
  icTagSignature desc_sig;
  CONSTRUCTOR_FUNCTION constructor_func;
  CLINKiccbased *pIccBasedInfo = NULL;
  int32 outputType = OUTPUT_NONE;
  int32 inputType = INPUT_NONE;
  uint8 intentUsed;

  HQASSERT( pInfo != NULL, "pInfo is NULL in get_iccbasedLink");
  HQASSERT( pInfo->n_device_colors > 0, "Invalid info in get_iccbasedLink" );

  if (desiredIntent >= N_ICC_TABLES) {
    HQFAIL("Invalid desiredIntent");
    desiredIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  }

  if (pInfo->devicelinkTablePresent) {
    /* A hack to make devicelink's always use the first table, NB. The first slot
     * here isn't Perceptual.
     */
    desiredIntent = 0;
  }

  intentUsed = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;

  /* See if we already have a CLINKiccbased. */
  /* If we do and it's -1, we've tried before and it's duff, so don't try again
   */
  if ( !isOutput )
    pIccBasedInfo = pInfo->dev2pcs[desiredIntent];
  else
    pIccBasedInfo = pInfo->pcs2dev[desiredIntent];

  if ( pIccBasedInfo != NULL ) {
    iccbasedInfoAssertions( pIccBasedInfo );
    return pIccBasedInfo;
  }

  /* not present, so make one */
  HQASSERT(IS_INTERPRETER(), "Attempting to use an ICC profile in the back end");

  /* It's possible that an RSD has been closed while the underlying file remains
   * open. This is a chance to recreate the RSD and continue.
   */
  if ( !set_icc_file( pInfo->filelist_head, pInfo->filelist_head->file ))
    goto tidyup;

  /* Now get more info from the profile */
  if ( !readTags( pInfo->filelist_head->file, &num_tags, &tags ))
    goto tidyup;

  /* Fill in the relative white and black points in the cache structure */
  if ( !get_rel_white_black_info( pInfo, num_tags, tags ))
    goto tidyup;

  /* All OK so far so allocate the CLINKiccbased */
  structSize = CLINKiccbased_SIZE(DEFAULT_ACTION_LIST_LENGTH);

  pIccBasedInfo = mi_alloc( structSize );
  if ( pIccBasedInfo == NULL )
    goto tidyup;

  /* Initialise and start to fill in the structure */
  ZEROINIT(pIccBasedInfo,CLINKiccbased);
  pIccBasedInfo->refCnt = 1;
  pIccBasedInfo->actions[0].u.remaining = DEFAULT_ACTION_LIST_LENGTH - 1;
  pIccBasedInfo->profile = pInfo;

  /* Create the common part of the CLINKiccbased */
  if ( !isOutput ) {
    pIccBasedInfo->iColorSpace = pInfo->devicespace;
    pIccBasedInfo->oColorSpace = pInfo->pcsspace;
    pIccBasedInfo->i_dimensions = pInfo->n_device_colors;
    pIccBasedInfo->o_dimensions = pInfo->n_pcs_colors;
  }
  else {
    pIccBasedInfo->iColorSpace = pInfo->pcsspace;
    pIccBasedInfo->oColorSpace = pInfo->devicespace;
    pIccBasedInfo->i_dimensions = pInfo->n_pcs_colors;
    pIccBasedInfo->o_dimensions = pInfo->n_device_colors;
  }

  /* Find out what type of scalings to apply before and after the icc profile
   * transforms.  Output profiles also need an inital scaling to the range
   * [0-1] before the first invoke.
   */
  if ( !isOutput ) {
    /* creating an input or devicelink */
    if ( pIccBasedInfo->iColorSpace == SPACE_Lab )
      inputType = INPUT_CONVERSION_LAB;
    else if ( pIccBasedInfo->iColorSpace == SPACE_DeviceCMY )
      inputType = INPUT_CMY;

    switch (pIccBasedInfo->oColorSpace) {
    case SPACE_ICCLab:
      outputType = OUTPUT_LAB;
      break;
    case SPACE_ICCXYZ:
      outputType = OUTPUT_XYZ;
      break;
    case SPACE_DeviceCMY:
      outputType = OUTPUT_CMY;
      break;
    default:
      outputType = OUTPUT_OTHER;
      break;
    }
  }
  else {
    /* creating an output link */
    HQASSERT( pIccBasedInfo->oColorSpace != SPACE_Lab,
              "Unexpected Lab colorspace in get_iccbasedLink" );

    if ( pIccBasedInfo->oColorSpace == SPACE_DeviceCMY )
      outputType = OUTPUT_CMY;

    switch (pIccBasedInfo->iColorSpace) {
    case SPACE_ICCLab:
      inputType = INPUT_LAB;
      break;
    case SPACE_ICCXYZ:
      inputType = INPUT_XYZ;
      break;
    default:
      inputType = INPUT_OTHER;
      break;
    }
  }

  /* Find out what type of profile it is */
  if ( !select_constructor( isOutput, num_tags, tags, pInfo->devicespace,
                            desiredIntent,
                            &intentUsed,
                            &desc_sig,
                            &outputType, &inputType,
                            &constructor_func ))
    goto tidyup;

  /* Start filling in the invoke structure */
  pIccBasedInfo->intent = intentUsed;

  /* Convert or scale from real world XYZ where necessary */
  switch (inputType) {
  case INPUT_CONVERSION_LAB:
    if ( !create_lab2icclab( &pIccBasedInfo ))
      goto tidyup;
    break;

  case INPUT_CMY:
    if ( !create_flip( &pIccBasedInfo, 3 ))
      goto tidyup;
    pIccBasedInfo->iColorSpace = SPACE_DeviceRGB;
    break;

  case INPUT_XYZ:
    if ( !create_ciexyz2iccxyz( &pIccBasedInfo, pInfo ))
      goto tidyup;
    break;

  case INPUT_CIE_XYZ:
    /* nothing to do - already in correct range */
    break;

  case INPUT_LAB:
  case INPUT_OLD_LAB:
    /* N.B. This does either icclab2ciexyz or vice versa */
    if ( !create_icclab2ciexyz( &pIccBasedInfo, pInfo, inputType ))
      goto tidyup;
    break;

#ifdef DEBUG_BUILD
  case INPUT_NONE:
    HQASSERT( !isOutput, "Unexpected output transform in get_iccbasedLink" );
    break;
#endif
  }

  /* Call the constructor function */
  if ( !constructor_func( &pIccBasedInfo, pInfo->filelist_head->file,
                          num_tags, tags, desc_sig))
    goto tidyup;

  /* Finish filling in the invoke structure */

  /* Convert or scale to real world XYZ where necessary */
  switch (outputType) {
  case OUTPUT_LAB:
  case OUTPUT_OLD_LAB:
    if ( !create_icclab2ciexyz( &pIccBasedInfo, pInfo, outputType ))
      goto tidyup;
    break;

  case OUTPUT_XYZ:
    if ( !create_iccxyz2ciexyz( &pIccBasedInfo, pInfo ))
      goto tidyup;
    break;

  case OUTPUT_CIE_XYZ:
    /* nothing to do - already in correct range */
    break;

  case OUTPUT_CMY:
    /* For CMY output (or devicelinks) invert the data */
    if ( !create_flip( &pIccBasedInfo, 3 ))
      goto tidyup;
    pIccBasedInfo->oColorSpace = SPACE_DeviceRGB;
    break;

#ifdef DEBUG_BUILD
  case OUTPUT_NONE:
    HQASSERT( isOutput, "Unexpected input transform in get_iccbasedLink" );
    break;
#endif
  }

  if ( inputType == INPUT_OLD_LAB ) {
    if ( !icc_probe_output_profile( pIccBasedInfo ))
      goto tidyup;
  }
  else if ( inputType == INPUT_LAB &&
            constructor_func == construct_lut8_16_invoke ) {
    /* Must have had a lut8Type */
    if ( !icc_probe_whitepoint( &pIccBasedInfo ))
      goto tidyup;
  }
  else if ( outputType == OUTPUT_OLD_LAB ) {
    if ( !icc_probe_input_profile( pIccBasedInfo ))
      goto tidyup;
  }

  iccbasedInfoAssertions( pIccBasedInfo );

  /* For the moment, we are filling the desired slot with a table that may not be
   * the one we asked for. This could be optimised by more use of reference
   * counting. But for now we'll just have copies of the tables in different slots
   * if, e.g. we have a TRC profile.
   */
  if ( !isOutput )
    pInfo->dev2pcs[desiredIntent] = pIccBasedInfo;
  else
    pInfo->pcs2dev[desiredIntent] = pIccBasedInfo;

  mi_free(tags);

  return pIccBasedInfo;

tidyup:
  mi_free(tags);
  if ( pIccBasedInfo )
    iccbased_free( pIccBasedInfo );
  return NULL;
}

/*----------------------------------------------------------------------------*/

static CLINK* create_iccbased_clink( ICC_PROFILE_INFO *pInfo,
                                     COLOR_STATE *colorState,
                                     Bool isOutput,
                                     uint8 desiredIntent )
{
  CLINK* pLink = NULL;
  CLINKiccbased* pIccBasedInfo = NULL;

  HQASSERT( pInfo, "Null pInfo in create_iccbased_clink" );
  HQASSERT( desiredIntent < N_ICC_TABLES,
    "Invalid desiredIntent");

  /* If we are in the process of preparing the back end for rendering, profiles
   * must be added to the colorState to avoid purging in low memory. Elsewhere
   * in the back end, this is a no-op.
   * NB. This place is not ideal, but as good as anywhere.
   */
  if (colorState != frontEndColorState) {
    if (!add_icc_info_to_cache(colorState, pInfo, TRUE))
      return NULL;
  }

  /* First get hold of the private data for the link */
  pIccBasedInfo = get_iccbasedLink( pInfo, isOutput, desiredIntent );

  /* This worked so now build the CLINK */
  if ( pIccBasedInfo != NULL ) {
    pLink = cc_common_create(pIccBasedInfo->i_dimensions,
                             NULL,
                             (COLORSPACE_ID)(isOutput ? pInfo->pcsspace : SPACE_ICCBased),
                             pIccBasedInfo->oColorSpace,
                             isOutput ? CL_TYPEiccbasedoutput : CL_TYPEiccbased,
                             iccbasedStructSize(),
                             &CLINKiccbased_functions,
                             1);
    if ( pLink ) {
      CLINK_RESERVE( pIccBasedInfo );
      pLink->p.iccbased = pIccBasedInfo;

      /** \todo FIXME: This will need fixing for 64bit pointers */
      pLink->idslot[0] = (CLID) pIccBasedInfo;
    }
  }

  return pLink;
}

/*----------------------------------------------------------------------------*/

/* Function read_colorantTable allocates and fills in a COLORANT_DATA
 * structure with the 'clrt' or 'clot' info from the profile.  In most cases if
 * it goes wrong return FALSE but don't raise an error so we can just go ahead
 * and use the tables instead.
 */
static Bool read_colorantTable( ICC_PROFILE_INFO *pInfo, int32 offset,
                                Bool clot )
{
#define header_size (4+4+4)       /* signature + count */
#define coord_size (3*sizeof(icUInt16Number))
#define entry_size sizeof(icColorantTableEntry)
#define name_size (32*sizeof(icInt8Number))

  size_t bytes_to_alloc;
  COLORANT_DATA *pcoloranttable = NULL;
  icColorantTableType temp;
  icColorantTableEntry temp_entry;
  uint32 i,j;
  FILELIST* flptr;

  HQASSERT( pInfo, "Null pInfo in read_colorantTable" );
  HQASSERT( pInfo->filelist_head, "Null filelist head in read_colorantTable" );

  flptr = pInfo->filelist_head->file;

  HQASSERT( flptr, "Null file pointer in read_colorantTable" );

  /* Seek to the tag */
  if (!mi_seek(flptr, offset)) {
    HQTRACE( TRUE, ("Unable to find colorantTableType tag in ICC profile.") );
    return TRUE;
  }

  /* read in the tag signature plus colorant count */
  if ( file_read(flptr, (uint8 *)&temp, header_size, NULL) <= 0) {
    HQTRACE( TRUE, ("Unable to read colorantTableType tag in ICC profile.") );
    return TRUE;
  }

  icfixsig(temp.base.sig);

  if (temp.base.sig != icSigColorantTableTag) {
    HQTRACE( TRUE, ("colorantTableType tag has incorrect type signature in ICC profile.") );
    return TRUE;
  }

  icfixu32( temp.colorantTable.count);

  /* Check this corresponds to what we expected */
  i = (!clot) ? pInfo->n_device_colors : pInfo->n_pcs_colors;
  if (temp.colorantTable.count != i) {
    HQTRACE( TRUE, ("colorantTableType tag has incorrect number of colors in ICC profile.") );
    return TRUE;
  }

  /* Try to allocate the memory */
  bytes_to_alloc = COLORANT_DATA_SIZE(temp.colorantTable.count);
  pcoloranttable = mi_alloc(bytes_to_alloc);
  if ( pcoloranttable == NULL )
    return FALSE;

  pcoloranttable->n_colors = (int8)temp.colorantTable.count;

  /* Get the names */
  for (i = 0; i < temp.colorantTable.count; i++) {
    if ( file_read(flptr, (uint8*) &temp_entry, entry_size, NULL) <= 0 )
      goto tidyup;

    /* Create the namecache entry */
    for (j = 0; j < name_size; j++) {
      if (temp_entry.colorantName[j] == 0) break;
    }

    if (j == 0) {
      /* XPS Named Color uses 3CLR profiles for 1, 2, or 3 Named Colors */
      pcoloranttable->colorantname[i] = &system_names[NAME_None];
    }
    else {
      pcoloranttable->colorantname[i] =
       cachename((const unsigned char*)&(temp_entry.colorantName), j);
    }
  }

  if (clot)
    pInfo->pcs_colorants = pcoloranttable;
  else
    pInfo->device_colorants = pcoloranttable;
  return TRUE;

tidyup:
  mi_free(pcoloranttable);
  return TRUE;
}


/* Get the colorants from the ColorantTable and ColorantTableOut tags */
static Bool get_colorants( ICC_PROFILE_INFO *pInfo )
{
  HQASSERT( pInfo != NULL, "pInfo is NULL in get_colorants");
  HQASSERT( pInfo->filelist_head != NULL,
            "Null filelist head in get_colorants");
  HQASSERT( pInfo->n_device_colors > 0, "Invalid Profile in get_colorants");

  /* See if we already have the COLORANT_DATA */
  if ( pInfo->device_colorants == 0 ) {
    int32 num_tags = 0;
    icTags *tags = NULL;
    icTags *p;

    HQASSERT( pInfo->pcs_colorants == 0, "Unexpected state in get_colorants");

    pInfo->device_colorants = COLORANTS_ABSENT;
    pInfo->pcs_colorants = COLORANTS_ABSENT;

    /* We need to get the info from the profile */
    if ( !readTags( pInfo->filelist_head->file, &num_tags, &tags ))
      return FALSE;

    /* Get the clrt tag if present */
    if ( (p = findTag(icSigColorantTableTag, num_tags, tags)) != NULL &&
         !read_colorantTable( pInfo, p->tag.offset, FALSE ) ) {
      mi_free(tags);
      return FALSE;
    }

    /* Get the clot tag too, if present */
    if ( (p = findTag(icSigColorantTableOutTag, num_tags, tags)) != NULL &&
         !read_colorantTable( pInfo, p->tag.offset, TRUE ) ) {
      mi_free(tags);
      return FALSE;
    }

    mi_free(tags);
  }

  return TRUE;
}


/* Try to create a dummy DeviceN colorspace for the profile output space.
 */
static Bool create_dummy_deviceN( Bool isOutput,
                                  ICC_PROFILE_INFO *pInfo,
                                  OBJECT **nextColorSpaceObject )
{
  corecontext_t *context = get_core_context_interp();
  Bool gallocmode;
  int32 i;
  OBJECT tempo = OBJECT_NOTVM_NOTHING, *arrayo;
  OBJECT *DeviceNobj;
  COLORANT_DATA *colorants;

  /* ensure file internal uses global VM (restore_ proof) */
  gallocmode = setglallocmode(context, TRUE);

  /* The tag is compulsory for device and output links going out to N color */
  if ( !get_colorants( pInfo ) )
    goto tidyup;

  colorants = (isOutput) ? pInfo->device_colorants : pInfo->pcs_colorants;
  DeviceNobj = (isOutput) ? &pInfo->dev_DeviceNobj : &pInfo->pcs_DeviceNobj;

  if ( colorants == COLORANTS_ABSENT ) {
    (void) detail_error_handler( SYNTAXERROR,
                                 "Unable to find colorants in ICC profile." );
    goto tidyup;
  }

  /* See if we already have the DeviceN colorspace */
  if ( oType( *DeviceNobj ) == ONULL ) {
    /* Construct the DeviceN colorspace in the form
     * [/DeviceN [/Name1 /Name2 ...] /DeviceCMYK {pop pop pop ... pop 0 0 0 1} ]
     * where we pop off all the colorant names.
     */

    if ( !ps_array( &tempo, 4 ) ||
         !ps_array( &oArray(tempo)[1], colorants->n_colors ))
      goto tidyup;

    object_store_name(&oArray(tempo)[0], NAME_DeviceN, LITERAL);

    /* The colorant array */
    arrayo = &oArray(tempo)[1];
    for ( i = 0; i < colorants->n_colors; i++ ) {
      oName(oArray(*arrayo)[i]) = colorants->colorantname[i];
      theTags(oArray(*arrayo)[i]) = ONAME|LITERAL;
    }

    object_store_name(&oArray(tempo)[2], NAME_DeviceCMYK, LITERAL);

    /* The tint transform */
    if ( !ps_array( &(oArray(tempo)[3]), (colorants->n_colors + 4)))
      goto tidyup;

    arrayo = &oArray(tempo)[3];
    theTags(*arrayo) |= EXECUTABLE;

    for ( i = 0; i< colorants->n_colors; i++ ) {
      object_store_operator(&oArray(*arrayo)[i], NAME_pop) ;
    }

    for ( i = 0; i < 3; i++ ) {
      object_store_integer(&oArray(*arrayo)[i+colorants->n_colors], 0) ;
    }

    object_store_integer(&oArray(*arrayo)[3 + colorants->n_colors], 1);

    Copy( DeviceNobj, &tempo );

    pInfo->devNsid = context->savelevel;
  }
#ifdef DEBUG_BUILD
  else {
    HQASSERT( oName( oArray(*DeviceNobj)[0] ) == &system_names[NAME_DeviceN],
              "Cached DeviceN space is no longer DeviceN in create_dummy_deviceN");
  }
#endif

  setglallocmode(context, gallocmode ) ;

  *nextColorSpaceObject = DeviceNobj;

  return TRUE;


tidyup:
  setglallocmode(context, gallocmode ) ;
  return FALSE;
}


/* Try to create a DeviceN colorspace to use instead of the profile */
static Bool create_deviceN( ICC_PROFILE_INFO *pInfo,
                            OBJECT *iccFileObject )
{
  corecontext_t *context = get_core_context_interp();
  int32 i, gallocmode;
  OBJECT tempo = OBJECT_NOTVM_NOTHING, *arrayo;
  OBJECT *iccColorSpaceObj;

  HQASSERT( pInfo, "Null pInfo in create_deviceN");
  HQASSERT( iccFileObject, "Null iccFileObject in create_deviceN" );

  /* ensure file internal uses global VM (restore_ proof) */
  gallocmode = setglallocmode(context, TRUE ) ;

  /* the clrt table is not compulsory, so don't return an error if absent */
  if ( !get_colorants( pInfo ) )
    goto tidyup;
  if ( pInfo->device_colorants == COLORANTS_ABSENT ) {
    pInfo->devicespace = SPACE_ICCBased;
    setglallocmode(context, gallocmode );
    return TRUE;
  }

  /* See if we already have the DeviceN colorspace */
  if ( oType( pInfo->dev_DeviceNobj ) == ONULL ) {
    /* Construct the DeviceN colorspace in the form
     * [/DeviceN [/Name1 /Name2 ...] [/ICCBased OFILE] {} ]
     */

    if ( !ps_array( &tempo, 4 ) ||
         !ps_array( &oArray(tempo)[1], pInfo->device_colorants->n_colors ))
      goto tidyup;

    object_store_name(&oArray(tempo)[0], NAME_DeviceN, LITERAL);

    arrayo = &oArray(tempo)[1];
    for ( i = 0; i < pInfo->device_colorants->n_colors; i++ ) {
      oName(oArray(*arrayo)[i]) = pInfo->device_colorants->colorantname[i];
      theTags(oArray(*arrayo)[i]) = ONAME|LITERAL;
    }

    /* The tint transform is an empty procedure (the identity) */
    if ( !ps_array( &(oArray(tempo)[3]), 0))
      goto tidyup;
    theTags(oArray(tempo)[3]) |= EXECUTABLE;

    Copy( &pInfo->dev_DeviceNobj, &tempo );

    pInfo->devNsid = context->savelevel;

    /* The AlternativeSpace is based on the orig_file ICCBased space. We've lost
     * colorspace array, only having the file object, but we're recreating the
     * array here.
     */
    iccColorSpaceObj = &oArray(pInfo->dev_DeviceNobj)[2];
    if (!ps_array( iccColorSpaceObj, 3))
      goto tidyup;

    /* Add a 3rd element to the DeviceN space of an empty dictionary. Ideally,
     * this would contain the uniqueID, but the mere presence of the dict is
     * enough to tell color chain construction that this is an "internal"
     * spaces in doDeviceN() that means we should be able to obtain a CMYK
     * equivalent from the profile.
     */
    oName(oArray(*iccColorSpaceObj)[0]) = &system_names[NAME_ICCBased];
    theTags(oArray(*iccColorSpaceObj)[0]) = ONAME|LITERAL;
    oFile(oArray(*iccColorSpaceObj)[1]) = oFile(*iccFileObject);
    theTags(oArray(*iccColorSpaceObj)[1]) = theTags(*iccFileObject);
    if (! ps_dictionary(&oArray(*iccColorSpaceObj)[2], 0))
      goto tidyup;
  }
#ifdef DEBUG_BUILD
  else {
    HQASSERT( oName( oArray(pInfo->dev_DeviceNobj)[0] ) == &system_names[NAME_DeviceN],
              "Cached DeviceN space is no longer DeviceN in create_deviceN");
  }
#endif

  setglallocmode(context, gallocmode ) ;

  return TRUE;

tidyup:
  setglallocmode(context, gallocmode ) ;
  return FALSE;
}

/*----------------------------------------------------------------------------*/
/* Probe the profile header to see if we have a colorspace conversion profile.
 * This is mainly to allow us to ask external CMMs if they handle these, where
 * appropriate.  There is a chance we may have already changed this flag to
 * icSigLinkClass in the case that a devicelink profile was mislabelled as
 * colorspace conversion, and that the external CMM may still be unable to
 * cope.  Hopefully though they will have set the retry flag, (at the mo we
 * always appear to act as if they have) so we will still do the conversion
 * using the internal CMM in the end.
 */
Bool cc_get_icc_is_conversion_profile( ICC_PROFILE_INFO *pInfo,
                                       int32 *is_conversion )
{
  HQASSERT( pInfo != NULL, "pInfo is Null in cc_get_icc_is_conversion_profile" );
  HQASSERT( is_conversion != NULL,
            "is_conversion is Null in cc_get_icc_is_conversion_profile" );

  /*** @@JJ we need better assertions about the validity of the info struct */
  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR, "Invalid colorspace in ICC profile.") ;

  *is_conversion = pInfo->deviceClass == icSigColorSpaceClass;

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Probe the profile header for the profile dimension and device colorspace */
Bool cc_get_icc_details( ICC_PROFILE_INFO *pInfo,
                         Bool onlyIfValid,
                         int32 *dimensions,
                         COLORSPACE_ID *deviceSpace,
                         COLORSPACE_ID *pcsSpace )
{
  HQASSERT( pInfo != NULL, "pInfo is Null in get_icc_details" );
  HQASSERT( dimensions != NULL, "dimensions is Null in get_icc_details" );
  HQASSERT( deviceSpace != NULL, "deviceSpace is Null in get_icc_details" );
  HQASSERT( pcsSpace != NULL, "pcsSpace is Null in get_icc_details" );

  /*** @@JJ we need better assertions about the validity of the info struct */
  if ( !pInfo->validProfile && (onlyIfValid || !pInfo->useAlternateSpace) )
    return detail_error_handler( SYNTAXERROR, "Invalid ICC profile." );

  *dimensions = pInfo->n_device_colors;
  *deviceSpace = pInfo->devicespace;
  *pcsSpace = pInfo->pcsspace;

  return TRUE;
}

/* This function is called to test the validity of a profile. It's useful for
 * those cases where cc_get_icc_details() has been called with onlyIfValid
 * is FALSE.
 */
Bool cc_valid_icc_profile( ICC_PROFILE_INFO *pInfo )
{
  HQASSERT( pInfo != NULL, "pInfo is Null in get_icc_details" );

  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR, "Invalid ICC profile." );

  return TRUE;
}

/* Probe the profile header for the profile dimension and device colorspace */
Bool cc_get_icc_DeviceN( ICC_PROFILE_INFO *pInfo,
                         OBJECT *iccFileObject,
                         OBJECT **nextColorSpaceObject )
{
  HQASSERT( pInfo != NULL, "pInfo is Null in get_icc_DeviceN" );
  HQASSERT( nextColorSpaceObject != NULL, "nextColorSpaceObject is Null in get_icc_DeviceN" );

  *nextColorSpaceObject = NULL;

  /*** @@JJ we need better assertions about the validity of the info struct */
  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR, "Invalid colorspace in ICC profile." );

  HQASSERT(pInfo->devicespace == SPACE_DeviceN, "Expected a DeviceN space");

  if (!create_deviceN(pInfo, iccFileObject))
    return FALSE;

  if (oType(pInfo->dev_DeviceNobj) == ONULL)
    *nextColorSpaceObject = NULL;
  else
    *nextColorSpaceObject = &pInfo->dev_DeviceNobj;

  return TRUE;
}

/* Probe the profile header for the profile dimension and device colorspace */
static Bool get_icc_profile_info( COLOR_STATE *colorState,
                                  OBJECT *iccFileObj,
                                  ICC_PROFILE_ID *uniqueID,
                                  ICC_PROFILE_INFO **ppInfo,
                                  int32 *dimensions,
                                  COLORSPACE_ID *deviceSpace,
                                  COLORSPACE_ID *pcsSpace )
{
  ICC_PROFILE_INFO *pInfo = NULL;

  HQASSERT( oType(*iccFileObj) == OFILE || oType(*iccFileObj) == OCPOINTER,
            "iccFileObj not a file in get_iccbased_profile_info" );
  HQASSERT( ppInfo != NULL, "ppInfo is Null in get_icc_profile_info" );
  HQASSERT( dimensions != NULL, "dimensions is Null in get_icc_profile_info" );
  HQASSERT( deviceSpace != NULL, "deviceSpace is Null in get_icc_profile_info" );
  HQASSERT( pcsSpace != NULL, "pcsSpace is Null in get_icc_profile_info" );

  *ppInfo = NULL;
  *dimensions = 0;
  *deviceSpace = SPACE_notset;
  *pcsSpace = SPACE_notset;

  /* probe the profile and return the device dimension and colorspace */
  if (oType(*iccFileObj) == OFILE)
    pInfo = get_cached_icc_info( colorState, iccFileObj, uniqueID );
  else {
    ICC_PROFILE_INFO_CACHE *cacheItem;

    /* For a back end colorState we can obtain the pInfo directly from the
     * custom safe back end CSA. We still need to make sure the profile is on
     * the ICC_cacheHead list to ensure the reference count is bumped once for
     * this page.
     */
    pInfo = oCPointer(*iccFileObj);
    for (cacheItem = colorState->ICC_cacheHead; cacheItem != NULL;
         cacheItem = cacheItem->next) {
      if (cacheItem->d == pInfo)
        break;
    }
    if (cacheItem == NULL) {
      if (!add_icc_info_to_cache( colorState, pInfo, TRUE ))
        return FALSE;
    }
  }
  if ( pInfo == NULL )
    return FALSE;

  if ( !pInfo->validProfile ) {
    /* This is a special "not quite complete failure". It is possible that the
     * client can make use of the cache for bad profiles when overriding them
     * with the nearest device space.
     */
    *ppInfo = pInfo;
    return detail_error_handler( SYNTAXERROR,
            "Invalid ICC profile." );
  }

  /* See if we should create a DeviceN for possible use later */
  if (pInfo->devicespace == SPACE_DeviceN ) {
    if ( !create_deviceN( pInfo, iccFileObj ))
      return FALSE;
  }

  *ppInfo = pInfo;
  *dimensions = pInfo->n_device_colors;
  *deviceSpace = pInfo->devicespace;
  *pcsSpace = pInfo->pcsspace;

  return TRUE;
}

/* Obtain the file object from the ICCBased CSA */
static void CSAtoFileObj(OBJECT       *iccbasedspace,
                         OBJECT       **iccFileObj)
{
  OBJECT *theo;

  theo = oArray( *iccbasedspace );

  HQASSERT( oType( *theo) == ONAME,
            "Expected name type in get_iccbased_profile_info" );
  HQASSERT( oName( *theo) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in get_iccbased_profile_info" );

  *iccFileObj = ++theo;
}

Bool cc_get_iccbased_profile_info( GS_COLORinfo *colorInfo,
                                   OBJECT *iccbasedspace,
                                   ICC_PROFILE_INFO **ppInfo,
                                   int32 *dimensions,
                                   COLORSPACE_ID *deviceSpace,
                                   COLORSPACE_ID *pcsSpace )
{
  int32 length;
  OBJECT *iccFileObj;
  ICC_PROFILE_ID uniqueID = {-1, 0};    /* Assume no uniqueID */
  OBJECT *alternateSpace = NULL;
  ICC_PROFILE_INFO_CACHE *cacheHead;
  COLOR_STATE *colorState = colorInfo->colorState;

  cacheHead = colorState->ICC_cacheHead;

  HQASSERT( oType( *iccbasedspace ) == OARRAY,
            "ICCBased space not an array in get_iccbased_profile_info" );
  HQASSERT( ppInfo != NULL,
            "ppInfo is Null in get_iccbased_profile_info" );
  HQASSERT( dimensions != NULL,
            "dimensions is Null in get_iccbased_profile_info" );
  HQASSERT( deviceSpace != NULL, "deviceSpace is Null in get_iccbased_profile_info" );
  HQASSERT( pcsSpace != NULL, "pcsSpace is Null in get_iccbased_profile_info" );

  CSAtoFileObj(iccbasedspace, &iccFileObj);

  length = theLen( *iccbasedspace );

  HQASSERT( length == 2 || length == 3,
            "Unexpected arraylength in get_iccbased_profile_info" );

  /* if there's a param dict, get the uniqueID */
  if ( length == 3 ) {
    OBJECT *obj;
    OBJECT *theo = oArray(*iccbasedspace);

    /* check it IS a dictionary */
    theo += 2;
    if (oType(*theo) != ODICTIONARY)
      return detail_error_handler( TYPECHECK,
              "Invalid ICCBased colorspace." );

    /* The XPS partname uid or a composite made from the PDF object and
     * generation numbers.
     */
    if ( (obj=fast_extract_hash_name(theo, NAME_XRef)) != NULL ) {
      HQASSERT( oType(*obj) == OINTEGER,
                "XRef not an integer in get_iccbased_profile_info");
      uniqueID.xref = oInteger(*obj);
    }

    /* The PDF execution context ID.  Get hold of this now even though its
     * value doesn't matter for uniqueID purposes if the XRef is not present.
     */
    if ( (obj=fast_extract_hash_name(theo, NAME_ContextID)) != NULL ) {
      HQASSERT( oType(*obj) == OINTEGER,
                "ContextID not an integer in get_iccbased_profile_info");
      uniqueID.contextID = oInteger(*obj);
    }

    /* The Alternate colorspace.  This is useful for handling invalid profiles
     * from PDF that we can nevertheless usefully override with a device space
     * that's obtained from the Alternate value. But only when we are overriding
     * color management.
     */
    if ( (obj=fast_extract_hash_name(theo, NAME_Alternate)) != NULL ) {
      alternateSpace = obj;
    }
  }

  if (!get_icc_profile_info(colorState, iccFileObj, &uniqueID, ppInfo,
                            dimensions, deviceSpace, pcsSpace)) {
    if (*ppInfo != NULL && alternateSpace != NULL) {
      /* The first time we get here for a profile from PDF/XPS, the alternate
       * space info is in the optional dict entry, so cache it. When that same
       * profile is later reused from the color chain cache, that dict might
       * have been stripped, so we'll use the cached info directly.
       */
      if (!gsc_getcolorspacesizeandtype(colorInfo, alternateSpace,
                                        deviceSpace, dimensions))
        return FALSE ;
      (*ppInfo)->useAlternateSpace = TRUE;
    }
    if (*ppInfo != NULL && (*ppInfo)->useAlternateSpace) {
      /* Even though we have a bad profile, we are returning true along with
       * the alternate space and the dimensions. This allows the client to
       * continue only in the case where ICC profiles are being overridden with
       * the nearest device space. Otherwise we will throw an error somewhere
       * else in a short while.
       */
      HQASSERT( !(*ppInfo)->validProfile, "The ICC profile shouldn't be valid");

      error_clear();

      /* Now frig pInfo to contain data of interest in the bad profile case */
      (*ppInfo)->devicespace = *deviceSpace;
      (*ppInfo)->pcsspace = *deviceSpace;
      (*ppInfo)->n_device_colors = CAST_SIGNED_TO_UINT8(*dimensions);
    }
    else
      return FALSE;
  }

  return TRUE;
}

Bool cc_icc_availableModes(ICC_PROFILE_INFO *pInfo,
                           Bool *inputTablePresent,
                           Bool *outputTablePresent,
                           Bool *devicelinkTablePresent)
{
  HQASSERT( pInfo != NULL, "pInfo is Null in cc_icc_availableModes" );
  HQASSERT( inputTablePresent != NULL, "input is Null in cc_icc_availableModes" );
  HQASSERT( outputTablePresent != NULL, "output is Null in cc_icc_availableModes" );
  HQASSERT( devicelinkTablePresent != NULL, "devicelink is Null in cc_icc_availableModes" );

  /*** @@JJ we need better assertions about the validity of the info struct */
  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR, "Invalid colorspace in ICC profile." );

  *inputTablePresent = pInfo->inputTablePresent;
  *outputTablePresent = pInfo->outputTablePresent;
  *devicelinkTablePresent = pInfo->devicelinkTablePresent;

  return TRUE;
}

int32 cc_iccbased_nOutputChannels(ICC_PROFILE_INFO *pInfo)
{
  HQASSERT( pInfo != NULL,
            "pInfo is Null in cc_iccbased_NOutputChannels" );
  HQASSERT(pInfo->pcsspace != SPACE_ICCXYZ && pInfo->pcsspace != SPACE_ICCLab,
           "Expected a devicelink profile");

  return pInfo->n_pcs_colors;
}

Bool cc_get_icc_output_profile_info( GS_COLORinfo *colorInfo,
                                     OBJECT *iccFileObj,
                                     ICC_PROFILE_INFO **ppInfo,
                                     int32 *dimensions,
                                     COLORSPACE_ID *deviceSpace,
                                     COLORSPACE_ID *pcsSpace )
{
  ICC_PROFILE_ID uniqueID = {-1, 0};    /* Assume no uniqueID */
  ICC_PROFILE_INFO_CACHE *cacheHead;
  COLOR_STATE *colorState = colorInfo->colorState;

  HQASSERT(IS_INTERPRETER(), "Calling cc_get_icc_output_profile_info() in the back end");

  cacheHead = colorState->ICC_cacheHead;

  HQASSERT( oType( *iccFileObj ) == OFILE,
            "iccFileObj not a file in cc_get_icc_output_profile_info" );
  HQASSERT( ppInfo != NULL,
            "ppInfo is Null in cc_get_icc_output_profile_info" );
  HQASSERT( dimensions != NULL, "dimensions is Null in cc_get_icc_output_profile_info" );
  HQASSERT( deviceSpace != NULL, "deviceSpace is Null in cc_get_icc_output_profile_info" );
  HQASSERT( pcsSpace != NULL, "pcsSpace is Null in cc_get_icc_output_profile_info" );

  return get_icc_profile_info(colorState, iccFileObj, &uniqueID, ppInfo,
                              dimensions, deviceSpace, pcsSpace);
}

Bool cc_iccSaveLevel(ICC_PROFILE_INFO *pInfo)
{
  HQASSERT( pInfo != NULL, "pInfo NULL" );
  if ( pInfo->filelist_head != NULL )
    return pInfo->filelist_head->sid;
  else
    return MAXSAVELEVELS;
}

/*----------------------------------------------------------------------------*/

sw_blob_instance *cc_get_icc_blob(ICC_PROFILE_INFO *pInfo)
{
  sw_blob_result result ;
  sw_blob_instance *blob ;

  HQASSERT( pInfo != NULL,
            "pInfo is Null in cc_get_icc_blob" );
  HQASSERT( pInfo->filelist_head != NULL,
            "filelist head is Null in cc_get_icc_blob" );

  /* We haven't bothered specialising the blob data store to ICC profiles. */
  if ( (result = blob_from_file(pInfo->filelist_head->file, SW_RDONLY,
                                global_blob_store, &blob)) != SW_BLOB_OK ) {
    (void)error_handler(error_from_sw_blob_result(result)) ;
    return NULL ;
  }

  HQASSERT(blob, "No blob returned, even though success reported") ;

  return blob ;
}

/*----------------------------------------------------------------------------*/
/* Returns the profileID (md5) for the profile header */
static Bool cc_get_iccbased_profile_header_ID( GS_COLORinfo *colorInfo,
                                               OBJECT *iccbasedspace,
                                               uint8 **header_md5 )
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;
  int32 N;

  HQASSERT( oType(*iccbasedspace) == OARRAY,
            "ICCBased space not an array in cc_get_iccbased_profile_header_ID" );
  HQASSERT( oType(*oArray(*iccbasedspace)) == ONAME,
            "Expected name type in cc_get_iccbased_profile_header_ID" );
  HQASSERT( oName(*oArray(*iccbasedspace)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in cc_get_iccbased_profile_header_ID" );

  /* get the info from the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, iccbasedspace, &pInfo, &N,
                                      &devicespace, &pcsspace ))
    return FALSE;

  /* we should have assigned a profile header ID by now */
  if ( !pInfo->validHeaderMD5 )
    return detail_error_handler( CONFIGURATIONERROR,
            "Problem handling profile ID" );

  *header_md5 = pInfo->header_md5;

  return TRUE;
}


/* Returns the profileID (md5) for the full profile, either as provided in the
 * profile header, or the one we have calculated.
 * If we have not yet calculated one, do it now.
 */
static Bool cc_get_iccbased_profileID(GS_COLORinfo *colorInfo,
                                      OBJECT *iccbasedspace, uint8 **md5 )
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;
  int32 N;

  HQASSERT( oType(*iccbasedspace) == OARRAY,
            "ICCBased space not an array in cc_get_iccbased_profileID" );
  HQASSERT( oType(*oArray(*iccbasedspace)) == ONAME,
            "Expected name type in cc_get_iccbased_profileID" );
  HQASSERT( oName(*oArray(*iccbasedspace)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in cc_get_iccbased_profileID" );

  /* get the info from the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, iccbasedspace, &pInfo, &N,
                                      &devicespace, &pcsspace ))
    return FALSE;

  /* we should have assigned a profile header ID by now */
  if ( !pInfo->validHeaderMD5 )
    return detail_error_handler( CONFIGURATIONERROR,
            "Problem handling profile ID" );

  /* we may need to calculate the full profile ID */
  if ( !pInfo->validMD5) {
    if ( !calculate_profile_MD5_from_profile_info(pInfo, FALSE) ||
         !pInfo->validMD5)
      return detail_error_handler( CONFIGURATIONERROR,
              "Problem handling profile ID" );
  }

  *md5 = pInfo->md5;

  return TRUE;
}

/* Compare the md5s of two profiles and see if they match */
Bool gsc_compare_md5s( GS_COLORinfo *colorInfo,
                       OBJECT *iccbasedspace_1,
                       OBJECT *iccbasedspace_2,
                       Bool *match )
{
  uint8 *md5_1 = NULL, *md5_2 = NULL;
  int32 i;

  HQASSERT( oType(*iccbasedspace_1) == OARRAY,
            "ICCBased space not an array in gsc_compare_md5s" );
  HQASSERT( oType(*oArray(*iccbasedspace_1)) == ONAME,
            "Expected name type in gsc_compare_md5s" );
  HQASSERT( oName(*oArray(*iccbasedspace_1)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in gsc_compare_md5s" );

  HQASSERT( oType(*iccbasedspace_2) == OARRAY,
            "ICCBased space not an array in gsc_compare_md5s" );
  HQASSERT( oType(*oArray(*iccbasedspace_2)) == ONAME,
            "Expected name type in gsc_compare_md5s" );
  HQASSERT( oName(*oArray(*iccbasedspace_2)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in gsc_compare_md5s" );

  HQASSERT( match != NULL, "Null match in gsc_compare_md5s" );

  *match = FALSE;

  /* If the header MD5s do not match neither can the profiles */
  if ( !cc_get_iccbased_profile_header_ID( colorInfo, iccbasedspace_1, &md5_1 ) ||
       !cc_get_iccbased_profile_header_ID( colorInfo, iccbasedspace_2, &md5_2 ))
    return FALSE;

  for ( i = 0; i < MD5_OUTPUT_LEN; ++i ) {
    if ( md5_1[i] != md5_2[i] )
      break;
  }

  if ( i == MD5_OUTPUT_LEN) {

    if ( !cc_get_iccbased_profileID( colorInfo, iccbasedspace_1, &md5_1 ) ||
         !cc_get_iccbased_profileID( colorInfo, iccbasedspace_2, &md5_2 ))
      return FALSE;

    for ( i = 0; i < MD5_OUTPUT_LEN; ++i ) {
      if ( md5_1[i] != md5_2[i] )
        break;
    }

    if ( i == MD5_OUTPUT_LEN )
      *match = TRUE;
  }

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Called from gsc_getcolorspacesizeandtype in the case of an ICCBased
 * colorspace, this routine returns N from the parameter dict, or probes the
 * profile to find the dimension.
 */
Bool gsc_get_iccbased_dimension( GS_COLORinfo *colorInfo,
                                 OBJECT *iccbasedspace, int32 *N )
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;

  HQASSERT( N != NULL, "N is Null in gsc_get_iccbased_dimension" );
  HQASSERT( oType(*iccbasedspace) == OARRAY,
            "ICCBased space not an array in gsc_get_iccbased_dimension" );
  HQASSERT( oType(*oArray(*iccbasedspace)) == ONAME,
            "Expected name type in gsc_get_iccbased_dimension" );
  HQASSERT( oName(*oArray(*iccbasedspace)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in gsc_get_iccbased_dimension" );

  /* get the info from the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, iccbasedspace, &pInfo, N,
                                      &devicespace, &pcsspace ))
    return FALSE;

  return TRUE;
}

/*----------------------------------------------------------------------------*/
/* Returns a namecache entry corresponding to the rendering intent field in the
 * profile's header.
 */
Bool gsc_get_iccbased_intent(GS_COLORinfo *colorInfo,
                             OBJECT *iccbasedspace, NAMECACHE **intentName)
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;
  int32 N;

  HQASSERT( oType(*iccbasedspace) == OARRAY,
            "ICCBased space not an array in gsc_get_iccbased_intent" );
  HQASSERT( oType(*oArray(*iccbasedspace)) == ONAME,
            "Expected name type in gsc_get_iccbased_intent" );
  HQASSERT( oName(*oArray(*iccbasedspace)) == &system_names[NAME_ICCBased],
            "Expected /ICCBased in gsc_get_iccbased_intent" );

  /* get the info from the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, iccbasedspace, &pInfo, &N,
                                      &devicespace, &pcsspace ))
    return FALSE;

  if (pInfo->preferredIntent == INVALID_INTENT)
    *intentName = system_names + NAME_RelativeColorimetric;
  else
    *intentName = gsc_convertIntentToName(pInfo->preferredIntent);

  return TRUE;
}


/* Take an ICCBased colorspace object from the operandstack, probe the profile
 * for the preferred intent from the header and and leave its name on the
 * stack.
 */
Bool gsc_geticcbasedintent(GS_COLORinfo *colorInfo, STACK *stack)
{
  OBJECT *theo, *otemp;
  NAMECACHE *renderingIntentName;
  int32 length;
  OBJECT ri = OBJECT_NOTVM_NOTHING;

  theo = theTop( *stack ) ;

  if ( oType( *theo ) != OARRAY )
    return detail_error_handler( TYPECHECK,
            "geticcbasedintent needs array type operand." );

  length = theLen( *theo );

  if ( length != 2 && length != 3 )
    return detail_error_handler( SYNTAXERROR,
            "Invalid array length for geticcbasedintent. ");

  otemp = oArray( *theo );

  if ( oName( *otemp ) != &system_names[NAME_ICCBased] )
    return detail_error_handler( SYNTAXERROR,
            "Name not /ICCBased in geticcbasedintent array. ");

  /* Probe the profile */
  if ( !gsc_get_iccbased_intent( colorInfo, theo, &renderingIntentName ))
      return FALSE;

  object_store_namecache(&ri, renderingIntentName, LITERAL);

  pop(stack);

  if (!push(&ri, stack))
    return FALSE;

  return TRUE;
}

/*----------------------------------------------------------------------------*/

/* Take an ICCBased colorspace object from the operandstack, probe the profile
 * for the number of device colorants and the device colorspace, and leave
 * these on the stack.
 */
Bool gsc_geticcbasedinfo(GS_COLORinfo *colorInfo, STACK *stack)
{
  OBJECT *theo, *otemp;
  int32 length, dimensions;
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;
  OBJECT cs = OBJECT_NOTVM_NOTHING;
  OBJECT dims = OBJECT_NOTVM_NOTHING;

  theo = theTop( *stack ) ;

  if ( oType( *theo ) != OARRAY )
    return detail_error_handler( TYPECHECK,
            "geticcbasedinfo needs array type operand." );

  length = theLen( *theo );

  if ( length != 2 && length != 3 )
    return detail_error_handler( SYNTAXERROR,
            "Invalid array length for geticcbasedinfo. ");

  otemp = oArray( *theo );

  if ( oName( *otemp ) != &system_names[NAME_ICCBased] )
    return detail_error_handler( SYNTAXERROR,
            "Name not /ICCBased in geticcbasedinfo array. ");

  /* Probe the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, theo, &pInfo, &dimensions,
                                      &devicespace, &pcsspace ))
    return FALSE;

  HQASSERT( dimensions > 0 && dimensions < 16,
            "Unexpected number of dimensions in gsc_geticcbasedinfo" );

  HQASSERT( devicespace == SPACE_DeviceGray || devicespace == SPACE_DeviceRGB  ||
            devicespace == SPACE_DeviceCMY  || devicespace == SPACE_DeviceCMYK ||
            devicespace == SPACE_DeviceN    || devicespace == SPACE_Lab,
            "Unexpected devicespace in gsc_geticcbasedinfo" );

  object_store_integer(&dims, dimensions);

  if ( devicespace == SPACE_DeviceGray )
    object_store_name(&cs, NAME_DeviceGray, LITERAL);
  else if ( devicespace == SPACE_DeviceRGB )
    object_store_name(&cs, NAME_DeviceRGB, LITERAL);
  else if ( devicespace == SPACE_DeviceCMY )
    object_store_name(&cs, NAME_DeviceCMY, LITERAL);
  else if ( devicespace == SPACE_DeviceCMYK )
    object_store_name(&cs, NAME_DeviceCMYK, LITERAL);
  else if ( devicespace == SPACE_DeviceN )
    object_store_name(&cs, NAME_DeviceN, LITERAL);
  else if (devicespace == SPACE_Lab )
    object_store_name(&cs, NAME_Lab, LITERAL);
  else
    return detail_error_handler( RANGECHECK,
            "Invalid device space in ICC profile header." );

  pop(stack);

  return push2(&dims, &cs, stack) ;
}

/*----------------------------------------------------------------------------*/
/* Probe the profile header to see if the devicespace is scRGB */
Bool cc_get_icc_is_scRGB( ICC_PROFILE_INFO *pInfo,
                          int32 *is_scRGB )
{
  HQASSERT( pInfo != NULL, "pInfo is Null in cc_get_icc_scRGBdetails" );
  HQASSERT( is_scRGB != NULL, "is_scRGB is Null in cc_get_icc_scRGBdetails" );

  /*** @@JJ we need better assertions about the validity of the info struct */
  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR, "Invalid colorspace in ICC profile.") ;

  *is_scRGB = pInfo->is_scRGB;

  return TRUE;
}

/* Take an ICCBased colorspace object from the operandstack, and probe it to
 * see if the devicespace is scRGB, leaving a Bool on the stack.
 */
Bool gsc_geticcbased_is_scRGB(GS_COLORinfo *colorInfo, STACK *stack)
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID devicespace;
  COLORSPACE_ID pcsspace;
  OBJECT *theo, *otemp;
  int32 N, length ;
  Bool is_scRGB = FALSE;

  theo = theTop( *stack ) ;

  if ( oType( *theo ) != OARRAY )
    return detail_error_handler( TYPECHECK,
            "geticcbasedisscrgb needs array type operand." );

  length = theLen( *theo );

  if ( length != 2 && length != 3 )
    return detail_error_handler( SYNTAXERROR,
            "Invalid array length for geticcbasedisscrgb.");

  otemp = oArray( *theo );

  if ( oName( *otemp ) != &system_names[NAME_ICCBased] )
    return detail_error_handler( SYNTAXERROR,
            "Name not /ICCBased in geticcbasedisscrgb array.");

  /* get the info from the profile */
  if ( !cc_get_iccbased_profile_info( colorInfo, theo, &pInfo, &N,
                                      &devicespace, &pcsspace ))
    return FALSE;

  /* Probe the profile */
  if ( !cc_get_icc_is_scRGB( pInfo, &is_scRGB ))
    return FALSE;

  pop(stack);

  return push(is_scRGB ? &tnewobj : &fnewobj, stack) ;
}

/** Query the device space of the passed ICC profile.
 *
 * \param iccFileObj ICC profile file.
 * \param deviceSpace This will be set to the ICC profile's device space.
 * \return FALSE on error.
 */
Bool gsc_get_icc_output_profile_device_space( GS_COLORinfo *colorInfo,
                                              OBJECT *iccFileObj,
                                              COLORSPACE_ID *deviceSpace )
{
  ICC_PROFILE_INFO *info;
  int32 dimensions;
  COLORSPACE_ID pcsSpace;

  return cc_get_icc_output_profile_info( colorInfo, iccFileObj, &info,
                                         &dimensions, deviceSpace, &pcsSpace );
}

/** Check if there exists a set of WCS (Windows Color System) profiles
    embedded in the ICC profile. */
Bool gsc_icc_check_for_wcs(FILELIST *iccp,
                           Bool *found, uint32 *offset, uint32 *size)
{
  int32 num_tags = 0 ;
  icTags *tags = NULL ;
  icTags *wcs_tag = NULL ;

  if ( !readTags(iccp, &num_tags, &tags ))
    return FALSE ;

  wcs_tag = findTag(icSigWcsProfilesTag, num_tags, tags) ;
  if ( found )
    *found = wcs_tag ? TRUE : FALSE ;
  if ( offset )
    *offset = wcs_tag ? wcs_tag->tag.offset : 0 ;
  if ( size )
    *size = wcs_tag ? wcs_tag->tag.size : 0 ;

  mi_free(tags) ;
  return TRUE ;
}

/*----------------------------------------------------------------------------*/

/* Creates a link for an input table using the same profile and intent as is
 * used in 'pLink'.
 * Useful for the black preservation functionality where we want to know the
 * K value that corresponds to the luminance of the output.
 */
CLINK *cc_createInverseICCLink(CLINK *pLink, COLORSPACE_ID *deviceSpace)
{
  CLINKiccbased *pIccBasedInfo;
  CLINK *inverseOutLink;
  COLORSPACE_ID oColorSpace;
  int32 dims;
  OBJECT *nextColorSpace;
  XYZVALUE *dummyWP;
  XYZVALUE *dummyBP;
  XYZVALUE *dummyRWP;
  XYZVALUE *dummyRBP;

  HQASSERT(pLink != NULL, "pLink NULL");
  HQASSERT(pLink->linkType == CL_TYPEiccbasedoutput, "pLink should be iccbasedoutput");

  pIccBasedInfo = pLink->p.iccbased;
  iccbasedInfoAssertions(pIccBasedInfo);

  if (!cc_iccbased_create(pIccBasedInfo->profile, NULL, pIccBasedInfo->intent,
                          &inverseOutLink, &oColorSpace, &dims, &nextColorSpace,
                          &dummyWP, &dummyBP, &dummyRWP, &dummyRBP))
    return NULL;

  *deviceSpace = pIccBasedInfo->profile->devicespace;

  return inverseOutLink;
}

/*----------------------------------------------------------------------------*/
/* Either, create a CLINK whose private data is of the CLINKiccbased type,
 * or, pass back a PS colorspace nextColorSpaceObject to use instead, or
 * in the case of a devicelink going out to DeviceN, do both.
 */
Bool cc_iccbased_create(ICC_PROFILE_INFO  *pInfo,                  /* IN  */
                        COLOR_STATE    *colorState,                /* IN */
                        uint8          desiredIntent,              /* IN  */
                        CLINK          **pNextLink,                /* OUT */
                        COLORSPACE_ID  *oColorSpace,               /* OUT */
                        int32          *dimensions,                /* OUT */
                        OBJECT         **nextColorSpaceObject,     /* OUT */
                        XYZVALUE       **sourceWhitePoint,         /* OUT */
                        XYZVALUE       **sourceBlackPoint,         /* OUT */
                        XYZVALUE       **sourceRelativeWhitePoint, /* OUT */
                        XYZVALUE       **sourceRelativeBlackPoint) /* OUT */
{
  /* For the moment, we are filling the desired slot with a table that may not be
   * the one we asked for. This could be optimised by more use of reference
   * counting. But for now we'll just have copies of the tables in different slots
   * if, e.g. we have a TRC profile.
   * Until this is sorted out, we cannot make use of intentUsed.
   */
  HQASSERT(pNextLink != NULL, "pNextLink is Null in cc_iccbased_create");
  HQASSERT( desiredIntent < N_ICC_TABLES,
    "Invalid desiredIntent");

  *nextColorSpaceObject = NULL;

  /* Really use the profile data */
  *pNextLink = create_iccbased_clink( pInfo, colorState, FALSE, desiredIntent );
  if ( *pNextLink == NULL )
    return FALSE;

  /* return the black and white points */
  *sourceWhitePoint = &pInfo->whitepoint;
  *sourceBlackPoint = &pInfo->blackpoint;
  *sourceRelativeWhitePoint = &pInfo->relative_whitepoint;
  *sourceRelativeBlackPoint = &pInfo->relative_blackpoint;

  /* If we have deviceN on the output side of the profile (it must be a devicelink),
   * then we need to construct the nextColorSpaceObject.
   */
  if ( pInfo->pcsspace == SPACE_DeviceN ) {
    if ( !create_dummy_deviceN( FALSE, pInfo, nextColorSpaceObject) ) {
      iccbased_destroy(*pNextLink);
      return FALSE;
    }
  }

  /* Fill in what other info we can */
  *oColorSpace = (*pNextLink)->p.iccbased->oColorSpace;
  *dimensions = (*pNextLink)->p.iccbased->o_dimensions;

  return TRUE;
}

/*----------------------------------------------------------------------------*/

/* Take an OFILE object referrring to an ICC profile, and a desired rendering
 * intent and pass back a CLINK for the output transform, ('crd' type transform
 * from XYZ/Lab to the device colorspace), and the intent actually used,
 * (for now always colorimetric if available in the profile).
 */
Bool cc_outputtransform_create(ICC_PROFILE_INFO *pInfo,                   /* IN/(OUT)*/
                               COLOR_STATE    *colorState,                /* IN */
                               uint8          desiredIntent,              /* IN */
                               OBJECT         **nextColorSpaceObject,     /* (IN)/OUT*/
                               CLINK          **pNextLink,                /* OUT */
                               COLORSPACE_ID  *oColorSpace,               /* OUT */
                               int32          *dimensions,                /* OUT */
                               XYZVALUE       **destWhitePoint,           /* OUT */
                               XYZVALUE       **destBlackPoint,           /* OUT */
                               XYZVALUE       **destRelativeWhitePoint,   /* OUT */
                               XYZVALUE       **destRelativeBlackPoint)   /* OUT */
{
  /* For the moment, we are filling the desired slot with a table that may not be
   * the one we asked for. This could be optimised by more use of reference
   * counting. But for now we'll just have copies of the tables in different slots
   * if, e.g. we have a TRC profile.
   * Until this is sorted out, we cannot make use of intentUsed.
   */
  HQASSERT(pInfo != NULL,
    "Null pInfo in cc_outputtransform_create");
  HQASSERT(nextColorSpaceObject != NULL,
    "Null nextColorSpaceObject in cc_outputtransform_create");
  HQASSERT(pNextLink != NULL, "pNextLink is Null in cc_outputtransform_create");
  HQASSERT( desiredIntent < N_ICC_TABLES,
    "Invalid desiredIntent in cc_outputtransform_create");

  *pNextLink = NULL;
  *nextColorSpaceObject = NULL;

  /* We can go no further if we were unable to read the header
   * or if there are no useable output tables.
   */
  if ( !pInfo->validProfile )
    return detail_error_handler( SYNTAXERROR,
                                 "Invalid colorspace in ICC profile.") ;

  if ( !pInfo->outputTablePresent )
    return detail_error_handler( CONFIGURATIONERROR,
                                 "ICC profile doesn't contain an output table.");

  /* Really use the profile data */
  *pNextLink = create_iccbased_clink( pInfo, colorState, TRUE, desiredIntent );
  if ( *pNextLink == NULL )
    return FALSE;

  /* return the black and white points */
  *destWhitePoint = &pInfo->whitepoint;
  *destBlackPoint = &pInfo->blackpoint;
  *destRelativeWhitePoint = &pInfo->relative_whitepoint;
  *destRelativeBlackPoint = &pInfo->relative_blackpoint;

  /* If we have deviceN on the output side of the profile,
   * then we need to construct the nextColorSpaceObject.
   */
  if ( pInfo->devicespace == SPACE_DeviceN ) {
    if ( !create_dummy_deviceN( TRUE, pInfo, nextColorSpaceObject) ) {
      iccbased_destroy(*pNextLink);
      return FALSE;
    }
  }

  /* Fill in what other info we can */
  *oColorSpace = (*pNextLink)->p.iccbased->oColorSpace;
  *dimensions = (*pNextLink)->p.iccbased->o_dimensions;
  return TRUE;
}

/*----------------------------------------------------------------------------*/

/**
 * Initialise an ICC list for a colorState.
 */
Bool gsc_startICCCache(COLOR_STATE *colorState)
{
  HQASSERT(colorState != NULL, "colorState NULL");

  colorState->ICC_cacheHead = NULL;

  return TRUE;
}

/**
 * Clear an ICC list for a colorState.
 */
void gsc_finishICCCache(COLOR_STATE *colorState)
{
  HQASSERT(colorState != NULL, "colorState NULL");

  while (colorState->ICC_cacheHead != NULL) {
    profile_discard(colorState, colorState->ICC_cacheHead->d, TRUE);
  }

  return;
}

/** We need to ensure that all ICC tables used in the back end will have been
 * unpacked because doing so requires filters which cannot be used in the back
 * end. The easiest way to achieve this is to build color chains for each group.
 * The act of building the chain will result in unpacking the table for the
 * rendering intent used in the profiles involved. We need to build chains for
 * each of the rendering intents and all profiles used in the job.
 * Object based color management may result in many profiles being potentially
 * used. It's necessary to build so many color chains because each chain may
 * involve a different profile and/or rendering intent. Doing this will guarantee
 * to cache all the profiles that are supplied to setinterceptcolorspace and
 * setreproduction in addition to profiles supplied by the job.
 * Black tint preservation can require additional ICC tables. These are unpacked
 * lazily during color chain invokes, rather than construction. So, it is
 * necessary to invoke each chain as well. The blackType must be BLACK_TYPE_TINT
 * and the reproType must be Other or Text for this to take effect, so we'll do
 * that unconditionally. The blackType doesn't otherwise matter for this function.
 */
Bool gsc_ICCCacheTransfer(DL_STATE *page)
{
  corecontext_t *context = get_core_context_interp();
  HDL_LIST *hlist;
  Bool result = FALSE;
  int32 colorType = GSC_BACKDROP;
  LateColorAttrib lca = lateColorAttribNew();

  if (!save_(context->pscontext))
    return FALSE;

  lca.origColorModel = REPRO_COLOR_MODEL_CMYK;
  lca.renderingIntent = SW_CMM_INTENT_RELATIVE_COLORIMETRIC;
  lca.overprintMode = FALSE;
  /* Must be BLACK_TYPE_TINT to transfer additional tables if BlackTint is in force. */
  lca.blackType = BLACK_TYPE_TINT;
  lca.independentChannels = FALSE;

  for (hlist = page->all_hdls; hlist != NULL; hlist = hlist->next) {
    Group *group = hdlGroup(hlist->hdl);

    /* No group, or matching raster styles, means no color conversion */
    if (group != NULL &&
        groupInputRasterStyle(group) != groupOutputRasterStyle(group)) {
      /* If BlackTint preservation is in force, the reproType has to be
       * either Other or Text in order to create the special ICC color
       * links required for black luminance on the output profile.
       */
      uint8 reproType = REPRO_TYPE_OTHER;

      /* Possibly optimise the 20 combinations of object type and color model
       * that are necessary with object based color management to just 1, if we
       * know there isn't any OBCM.
       * The test is on group->colorInfo which will contain the same contents
       * as group->preconvert->colorInfo, but note that the chains will
       * actually be invoked using the latter.
       */
      Bool usingObjectBasedColor = cc_usingObjectBasedColor(groupColorInfo(group));

      if (guc_backdropRasterStyle(groupOutputRasterStyle(group))) {
        LateColorAttrib *groupLCA;

        /* For backdrop groups that require ICC tables, the rendering intent and
         * color model assigned to the group is the one that is used for all
         * objects within the group. We must still evaluate all object types
         * unless usingObjectBasedColor is off.
         */
        groupLCA = groupGetAttrs(group)->lobjLCA;
        if (groupLCA != NULL) {
          lca.renderingIntent = groupLCA->renderingIntent;
          lca.origColorModel = groupLCA->origColorModel;

          if (!usingObjectBasedColor) {
            if (!preconvert_invoke_all_colorants(group, colorType, reproType, &lca))
              goto tidyup;
          }
          else {
            for (reproType = 0; reproType < REPRO_N_TYPES; reproType++) {
              if (!preconvert_invoke_all_colorants(group, colorType, reproType, &lca))
                goto tidyup;
            }
          }
        }
        else {
          HQASSERT(groupGetUsage(group) != GroupPage &&
                   groupGetUsage(group) != GroupSubGroup &&
                   groupGetUsage(group) != GroupImplicit,
                   "Group was expected to have an LCA");
        }
      }
      else {
        uint8 ri;

        /* The one, non-backdrop, group. Objects may use any rendering intent
         * or color model, so we must evaluate all possible color chains to
         * guarantee transfering all required ICC tables; unless we can reduce
         * the set with usingObjectBasedColor.
         */

        /* For each rendering intent */
        for (ri = 0; ri < N_ICC_RENDERING_TABLES; ri++) {
          lca.renderingIntent = ri;

          if (!usingObjectBasedColor) {
            lca.origColorModel = REPRO_COLOR_MODEL_CMYK;  /* Random, doesn't matter */
            if (!preconvert_invoke_all_colorants(group, colorType, reproType, &lca))
              goto tidyup;
          }
          else {
            /* For each object type */
            for (reproType = 0; reproType < REPRO_N_TYPES; reproType++) {
              REPRO_COLOR_MODEL cm;

              /*  For each color mode */
              for (cm = 0; cm < REPRO_N_COLOR_MODELS; cm++) {
                lca.origColorModel = cm;
                if (!preconvert_invoke_all_colorants(group, colorType, reproType, &lca))
                  goto tidyup;
              }
            }
          }
        }
      }
    }
  }

  result = TRUE;

tidyup:
  /* At this point, the operandstack holds a save object from the save_ at the
   * beginning of this function.
   */
  (void) restore_(context->pscontext);

  return result;
}

void gsc_safeBackendColorSpace(OBJECT *dstColorSpace,
                               OBJECT *srcColorSpace,
                               OBJECT *dstColorSpaceArray)
{
  ICC_PROFILE_INFO *pInfo;
  COLORSPACE_ID csId;
  ICC_PROFILE_ID uniqueID = {-1, 0};    /* Assume no uniqueID */

  HQASSERT(IS_INTERPRETER(), "Attempting to use an ICC profile in the back end");
  HQASSERT(oType(*dstColorSpace) == ONULL, "Expected a null RS color space");

  if (oType(*srcColorSpace) == ONULL) {
    Copy(dstColorSpace, srcColorSpace);
    return;
  }

  if (!gsc_getcolorspacetype(srcColorSpace, &csId)) {
    HQFAIL("Unexpected error in RS color space");
    return;
  }

  switch (csId) {
  case SPACE_ICCBased:
    if ( theTags(oArray(*srcColorSpace)[1]) == OCPOINTER ) {
      pInfo = oCPointer(oArray(*srcColorSpace)[1]) ;
    }
    else {
      pInfo = find_cached_icc_info(frontEndColorState,
                                   &oArray(*srcColorSpace)[1], &uniqueID);
    }

    if (pInfo == NULL) {
      HQFAIL("Should have found an ICC cache entry");
      return;
    }

    /* Construct special CSA that contains a C pointer to the ICC_PROFILE_INFO.
     * as opposed to a file object as per normal. This will be recognised as a
     * back end ICCBased color space.
     */
    Copy(&dstColorSpaceArray[0], &oArray(*srcColorSpace)[0]);
    theTags(dstColorSpaceArray[1]) = OCPOINTER;
    oCPointer(dstColorSpaceArray[1]) = pInfo;

    theTags(*dstColorSpace) = OARRAY | UNLIMITED;
    theLen(*dstColorSpace) = 2;
    oArray(*dstColorSpace) = dstColorSpaceArray;
    break;
  case SPACE_DeviceCMYK:
  case SPACE_DeviceRGB:
  case SPACE_DeviceGray:
    Copy(dstColorSpace, srcColorSpace);
    break;
  default:
    HQFAIL("Unexpected kind of RS color space");
    break;
  }
}

/*----------------------------------------------------------------------------*/

void init_C_globals_gscicc(void)
{
  ICCCacheRoot = NULL ;
}

/* Log stripped */
