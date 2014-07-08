/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:gsccrd.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Color Rendering Dictionary (CRD) support
 */

#include "core.h"

#include "constant.h"           /* EPSILON */
#include "dictscan.h"           /* NAMETYPEMATCH */
#include "gcscan.h"             /* ps_scan_field */
#include "gu_chan.h"            /* DEVICESPACEID */
#include "hqmemcpy.h"           /* HqMemCpy */
#include "matrix.h"             /* matrix_inverse_3x3 */
#include "mm.h"                 /* mm_sac_alloc */
#include "mps.h"                /* mps_res_t */
#include "namedef_.h"           /* NAME_ColorRenderingType */
#include "objects.h"            /* NEWICSAVEDOBJECT */
#include "swerrors.h"           /* RANGECHECK */

#include "gs_colorpriv.h"       /* extractN */
#include "gschead.h"            /* gsc_getcolorspacesizeandtype */
#include "gschcmspriv.h"        /* cc_invalidateCurrentRenderingIntent */
#include "gsccrdpriv.h"         /* extern's */
#include "functns.h"
#include "gs_callps.h"
#include "monitor.h"

typedef struct ENCODEABC_DATA {
  Bool          present;
  SYSTEMVALUE   mul;
  SYSTEMVALUE   sub;
  SYSTEMVALUE   div;
  OBJECT        array;
} ENCODEABC_DATA;

typedef struct ENCODELMN_DATA {
  Bool          present;
  SYSTEMVALUE   div;
  SYSTEMVALUE   le;
  SYSTEMVALUE   mul;
} ENCODELMN_DATA;

typedef struct RENDERTABLE_DATA {
  Bool          present;
  SYSTEMVALUE   div;
  OBJECT        array;
} RENDERTABLE_DATA;

struct CLINKcrd {
  GS_CRDinfo    *crdInfo;
  Bool          doPQR;
  Bool          doSimplePQR;
  Bool          neutralMappingNeeded;
  XYZVALUE      sourceWP;
  XYZVALUE      sourceBP;
  OBJECT        Ws, Bs, Wd, Bd;   /* the array objects themselves */
  OBJECT        tristimulus[24];  /* storage for the arrays Ws, Bs, Wd, Bd
                                   * referred to on page 300 of red book 2 */
};

struct GS_CRDinfo {
  cc_counter_t  refCnt;
  size_t        structSize;

  /* Most of the data below is extracted from a crd dictionary */
  int32         dimension;
  COLORSPACE_ID outputColorSpaceId;
  OBJECT        outputColorSpace;

  Bool          encodeabcPresent;
  Bool          matrixabcPresent;
  Bool          encodelmnPresent;
  Bool          matrixlmnPresent;
  Bool          matrixpqrPresent;
  Bool          renderTablePresent;
  Bool          renderTableHasData;

  OBJECT        colorrendering;

  int32         convertXYZtoLabMethod;

  SYSTEMVALUE   rangeabc[6];
  OBJECT        encodeabc[3];
  CIECallBack   encodeabcfn[3];
  ENCODEABC_DATA  encodeabcData[3];
  SYSTEMVALUE   matrixabc[9];

  SYSTEMVALUE   rangelmn[6];
  OBJECT        encodelmn[3];
  CIECallBack   encodelmnfn[3];
  ENCODELMN_DATA  encodelmnData[3];
  SYSTEMVALUE   matrixlmn[9];

  XYZVALUE      whitepoint;
  XYZVALUE      blackpoint;
  XYZVALUE      relativewhitepoint;
  XYZVALUE      relativeblackpoint;

  SYSTEMVALUE   matrixpqr[9];
  SYSTEMVALUE   rangepqr[6];
  OBJECT        transformpqr[3];
  CIECallBack   transformpqrfn[3];

  SYSTEMVALUE   matrixpqrinv[9];

  CALLPSCACHE   *cpsc_encodelmn[3];
  CALLPSCACHE   *cpsc_encodeabc[3];
  CALLPSCACHE   *cpsc_tiproc[4];

  Bool          renderTable16bit;
  OBJECT        rendertable;
  CIECallBack   *renderTablefn;           /* [M] */
  RENDERTABLE_DATA  *renderTableData;     /* [M] */

  /* XUID/UniqueID. */
  int32         nXUIDs ;
  int32         *pXUIDs ;
};

/* Defines for convertXYZtoLabMethod */
#define CONVERT_XYZ_WP            1
#define CONVERT_XYZ_RELATIVE_WP   2
#define CONVERT_XYZ_NONE          3

/* The difference between input & output XYZ values that forces a PQR (chromatic
 * adaptation phase. If less than this, simple XYZ scaling is deemed sufficiently
 * accurate.
 */
#define XYZ_EPSILON       0.001

static void  crd_destroy(CLINK *pLink);
static Bool crd_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);
#ifdef INVOKEBLOCK_NYI
static Bool crd_invokeBlock( CLINK *pLink , CLINKblock *pBlock );
#endif /* INVOKEBLOCK_NYI */
static mps_res_t crd_scan( mps_ss_t ss, CLINK *pLink );

#if defined( ASSERT_BUILD )
static void crdAssertions(CLINK *pLink);
static void crdInfoAssertions(GS_CRDinfo *pInfo);
#else
#define crdAssertions(_pLink) EMPTY_STATEMENT()
#define crdInfoAssertions(_pInfo) EMPTY_STATEMENT()
#endif

static void convertXYZtoLab(XYZVALUE cie, XYZVALUE white);
static void setupPQR(CLINKcrd *crd);
static Bool doPQR(XYZVALUE cie, CLINKcrd *crd, GS_CRDinfo  *crdInfo);

static Bool cc_setcolorrendering_1( GS_COLORinfo  *colorInfo,
                                    GS_CRDinfo    **pcrdInfo,
                                    OBJECT        *crdObject );
static Bool cc_setcolorrendering_2( GS_COLORinfo *colorInfo, OBJECT *crdObject );

static Bool identityMatrix(SYSTEMVALUE matrix[9]);

CIECallBack findEncodeabcProc(OBJECT *proc, ENCODEABC_DATA *encodeabcData);
static SYSTEMVALUE encodeabc_ICC_1(SYSTEMVALUE arg, void *extra);
static SYSTEMVALUE encodeabc_ICC_2(SYSTEMVALUE arg, void *extra);
static SYSTEMVALUE encodeabc_Hqn_1(SYSTEMVALUE arg, void *extra);
static SYSTEMVALUE encodeabc_Hqn_2(SYSTEMVALUE arg, void *extra);
CIECallBack findEncodelmnProc(OBJECT *proc, ENCODELMN_DATA *encodelmnData);
static Bool lmnIfArrayCheck(OBJECT *array);
static Bool lmnElseArrayCheck(OBJECT *array);
static SYSTEMVALUE encodelmn_LN(SYSTEMVALUE arg, void *extra);
static SYSTEMVALUE encodelmn_M(SYSTEMVALUE arg, void *extra);
CIECallBack findRenderTableProc(OBJECT *proc, RENDERTABLE_DATA *renderTableData);
static SYSTEMVALUE rendertable_1(SYSTEMVALUE val, void *extra);
static SYSTEMVALUE rendertable_2(SYSTEMVALUE val, void *extra);
static Bool interpopCheck(OBJECT *array);
static SYSTEMVALUE interpop(SYSTEMVALUE arg, OBJECT *array);
static Bool abcopCheck(OBJECT *array);
static SYSTEMVALUE abcop(SYSTEMVALUE val, OBJECT *array);

#if defined( ASSERT_BUILD )
static Bool crdPSCallbackTrace;
#endif

static CLINKfunctions CLINKcrd_functions =
{
  crd_destroy,
  crd_invokeSingle,
  NULL /* crd_invokeBlock */,
  crd_scan
};

/* ---------------------------------------------------------------------- */

/*
 * CRD Link Data Access Functions
 * ==============================
 */

CLINK *cc_crd_create(GS_CRDinfo         *crdInfo,
                     XYZVALUE           sourceWhitePoint,
                     XYZVALUE           sourceBlackPoint,
                     XYZVALUE           **destWhitePoint,
                     XYZVALUE           **destBlackPoint,
                     XYZVALUE           **destRelativeWhitePoint,
                     DEVICESPACEID      realDeviceSpace,
                     Bool               neutralMappingNeeded,
                     OBJECT             **ppOutputColorSpace,
                     COLORSPACE_ID      *oColorSpace,
                     int32              *colorspacedimension) /* of the output colorspace */
{
  int32     i;
  int32     nXUIDs;
  CLINK     *pLink;
  CLINKcrd  *crd;

  HQASSERT(crdInfo != NULL, "NULL crdInfo");
  HQASSERT(crdInfo->dimension > 0, "crdInfo has illegal output dimensions");
  HQASSERT(sourceWhitePoint != NULL, "NULL sourceWhitePoint");
  HQASSERT(sourceBlackPoint != NULL, "NULL sourceBlackPoint");
  HQASSERT(ppOutputColorSpace != NULL, "NULL ppOutputColorSpace");
  HQASSERT(oColorSpace != NULL, "NULL oColorSpace");
  HQASSERT(colorspacedimension != NULL, "NULL colorspacedimension");

  /* We need to identify all the unique characteristics that identify a CLINK.
   * The CLINK type, colorspace, colorant set are orthogonal to these items.
   * The device color space & colorants (and so x/ydpi) are defined as fixed.
   * For CL_TYPEcrd (looking at invokeSingle) we have:
   * a) nXUIDs pXUIDs (nx32bits).
   * =
   * n slots.
   */

  HQASSERT( crdInfo != NULL, "crdInfo was NULL in cc_crd_create" );
  *colorspacedimension = crdInfo->dimension ;

  if (!crdInfo->renderTablePresent && crdInfo->dimension == 3) {
    /* In this case the output depends on whether we are going to DeviceGray or
       DeviceRGB. In the former case we take only the first component of the output */

    if (realDeviceSpace == DEVICESPACE_Gray) {
      if (crdInfo->outputColorSpaceId != SPACE_DeviceGray) {
        object_store_name(&crdInfo->outputColorSpace, NAME_DeviceGray, LITERAL);
        crdInfo->outputColorSpaceId = SPACE_DeviceGray;
      }
      *colorspacedimension = 1; /* we are going to DeviceGray, so we will drop the
                                   other two possible outputs for the crd, according
                                   *to the red book rules */
    } else {
      if (crdInfo->outputColorSpaceId != SPACE_DeviceRGB) {
        object_store_name(&crdInfo->outputColorSpace, NAME_DeviceRGB, LITERAL);
        crdInfo->outputColorSpaceId = SPACE_DeviceRGB;
      }
    }
  }

  *oColorSpace = crdInfo->outputColorSpaceId;
  *ppOutputColorSpace = & crdInfo->outputColorSpace;

  nXUIDs = crdInfo->nXUIDs;

  pLink = cc_common_create(NUMBER_XYZ_COMPONENTS,
                           NULL,
                           SPACE_CIEXYZ,
                           *oColorSpace,
                           CL_TYPEcrd,
                           0,
                           &CLINKcrd_functions,
                           nXUIDs);
  if (pLink == NULL)
    return NULL;

  pLink->p.crd = mm_sac_alloc(mm_pool_color,
                              sizeof(CLINKcrd),
                              MM_ALLOC_CLASS_NCOLOR);
  if (pLink->p.crd == NULL) {
    crd_destroy(pLink);
    (void) error_handler(VMERROR);
    return NULL;
  }

  crd = pLink->p.crd ;
  crd->crdInfo = crdInfo;

  crd->neutralMappingNeeded = neutralMappingNeeded;

  for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++) {
    crd->sourceWP[i] = sourceWhitePoint[i];
    crd->sourceBP[i] = sourceBlackPoint[i];
    HQASSERT(sourceWhitePoint[i] > 0.0, "Unexpected White point");
  }

  setupPQR(crd);

  while ((--nXUIDs) >= 0)
    pLink->idslot[nXUIDs] = crdInfo->pXUIDs[nXUIDs];

  /* return the black and white points */
  *destWhitePoint = &crdInfo->whitepoint;
  *destBlackPoint = &crdInfo->blackpoint;
  *destRelativeWhitePoint = &crdInfo->relativewhitepoint;

  crdAssertions(pLink);

  return pLink;
}

static void crd_destroy(CLINK *pLink)
{
  crdAssertions(pLink);

  if (pLink->p.crd != NULL)
    mm_sac_free(mm_pool_color, pLink->p.crd, sizeof (CLINKcrd));

  cc_common_destroy(pLink);
}

Bool crd_callps(OBJECT *psobj, SYSTEMVALUE *val)
{
  if ( !call_psproc(psobj, val[0], val, 1) )
    return FALSE;
  return TRUE;
}

/**
 * Check that the accuracy of the call PS cache is sufficient by calling the
 * ps func and comparing the result. Can only be safely used with single
 * threaded compositing
 */
#define VERIFY_CALLPS_CACHE 0

/**
 * Lookup a value in the cache of results of calling the various
 * PS sub-functions.
 */
static void crd_lookup(CALLPSCACHE *cpsc, USERVALUE in,
                       USERVALUE *out, OBJECT *psproc)
{
#if VERIFY_CALLPS_CACHE
  SYSTEMVALUE sv = (SYSTEMVALUE)in;

  (void)crd_callps(psproc, &sv);
#endif /* VERIFY_CALLPS_CACHE */

  UNUSED_PARAM(OBJECT *, psproc);
  lookup_callpscache(cpsc, in, out);

#if VERIFY_CALLPS_CACHE
  {
    SYSTEMVALUE diff = sv - (SYSTEMVALUE)out[0];

    if ( diff < 0.0 ) { diff = -diff; }
    diff = diff * 256.0;
    if ( diff > 1.0 )
      monitorf((uint8 *)"CRD callpscache : %.1f %.7f %.7f\n", diff,
          sv, out[0]);
  }
#endif /* VERIFY_CALLPS_CACHE */
}

static Bool crd_invokeSingle(CLINK *pLink, USERVALUE *oColorValues)
{
#define CLIST(_n_, _precision_) \
  (precision == 2 ? (256 * clist[(_n_)] + clist[(_n_ + 1)]) : clist[(_n_)])

  /* this routine implements the two boxes in the top row of the diagrams
     in figures 4.5 and 4.6 in red book 2, pages 178-179. It is basically
     a matter of just ploughing through the transformations as specified
     missing out as many as we can where they're optional and not supplied.
     All the range checks are done as these are filled in by default rather
     than omitted from the graphics state.

     All the type checking has been done. It is possible someone has poked the
     things with something different, but it's already so costly to do this
     that I don't want to introduce yet more overhead by doing it again.
     The risk is a crash or other garbage. */

  int32       i;
  OBJECT      *theo = NULL;
  OBJECT      *theproc = NULL;
  CLINKcrd    *crd;
  GS_CRDinfo  *crdInfo;
  int32       m;               /* the m referred to on page 306 */
  int32       na, nb, nc;      /* dimensions of render table */
  SYSTEMVALUE arg;             /* temporary variable */
  XYZVALUE    cie;             /* the result so far */
  int32       precision = 0;   /* 1 or 2, for 8 or 16 bit render table */
  int32       b_dist = 0;
  int32       c_dist = 0;
  int32       diag_dist = 0;

  crdAssertions(pLink);
  HQASSERT(oColorValues != NULL, "oColorValues == NULL");

  crd = pLink->p.crd;
  crdInfo = crd->crdInfo;

  for (i = 0; i < 3; i++)
    cie[i] = pLink->iColorValues[i];

  /* At this point we should have the CIE 1931 XYZ values in cie[]
     Proceed to convert them to whichever color space is specified by the
     rendering dictionary */

  /* -------------------- STEP 2 ON PAGE 297: -------------------- */

  if (crd->neutralMappingNeeded) {
    /* see if we need to do anything at all by comparing input and output
       white and black points */
    if (crd->doPQR) {
      if (!doPQR(cie, crd, crdInfo))
        return FALSE;
    }
  }

  /* -------------------- STEP 3 ON PAGE 297: -------------------- */

  /* convert from XYZ to Lab if necessary */
  if (crdInfo->convertXYZtoLabMethod == CONVERT_XYZ_RELATIVE_WP)
    /* This is for ICC profiles which will convert absolute XYZ in the Postscript
     * connection space to media-relative
      Lab values.
     */
    convertXYZtoLab(cie, crdInfo->relativewhitepoint);
  else if (crdInfo->convertXYZtoLabMethod == CONVERT_XYZ_WP)
    convertXYZtoLab(cie, crdInfo->whitepoint);
  else
    HQASSERT(crdInfo->convertXYZtoLabMethod == CONVERT_XYZ_NONE, "Unexpected XYZ to Lab method");

  /* multiply by MatrixLMN */
  if (crdInfo->matrixlmnPresent) {
    MATRIX_MULTIPLY(cie, crdInfo->matrixlmn);
  }

  /* call the EncodeLMN procedures */
  if (crdInfo->encodelmnPresent) {
    for (i = 0; i < 3; i++) {
      if (crdInfo->encodelmnData[i].present)
        CALL_C(crdInfo->encodelmnfn[i], cie[i], &crdInfo->encodelmnData[i]);
      else if (crdInfo->encodelmnfn[i] != NULL)
        CALL_C(crdInfo->encodelmnfn[i], cie[i], NULL);
      else {
        USERVALUE uval = (USERVALUE)cie[i];

        crd_lookup(crdInfo->cpsc_encodelmn[i], uval, &uval,
                   crdInfo->encodelmn + i);
        cie[i] = (SYSTEMVALUE)uval;
      }
    }
  }

  /* narrow to RangeLMN */
  NARROW3 (cie, crdInfo->rangelmn);

  /* multiply by MatrixABC */
  if (crdInfo->matrixabcPresent) {
    MATRIX_MULTIPLY (cie, crdInfo->matrixabc);
  }

  /* call the EncodeABC procedures */
  if (crdInfo->encodeabcPresent) {
    for (i = 0; i < 3; i++) {
      if (crdInfo->encodeabcData[i].present)
        CALL_C(crdInfo->encodeabcfn[i], cie[i], &crdInfo->encodeabcData[i]);
      else if (crdInfo->encodeabcfn[i] != NULL)
        CALL_C(crdInfo->encodeabcfn[i], cie[i], NULL);
      else {
        USERVALUE uval = (USERVALUE)cie[i];

        lookup_callpscache(crdInfo->cpsc_encodeabc[i], uval, &uval);
        cie[i] = (SYSTEMVALUE)uval;
      }
    }
  }

  /* -------------------- STEP 4 ON PAGE 297: -------------------- */
  if (crdInfo->renderTablePresent) {
    theo = oArray(crdInfo->rendertable);
    na = oInteger(theo[0]);
    nb = oInteger(theo[1]);
    nc = oInteger(theo[2]);
    m =  crdInfo->dimension;
    precision = (crdInfo->renderTable16bit ? 2 : 1);
    theproc = theo + 5;
  } else {
    na = nb = nc = 0;
    m = 3;
    /* special case: only the first of the three outputs is selected if we are going
       to DeviceGray. This can only arise because the context in which the CRD is
       used is to DeviceGray, rather than anything in the CRD itself. A Harlequin
       extension allows for DeviceGray to be specified explicitly in the m slot of
       the RenderTable, but here there is no RenderTable */
    if (crdInfo->outputColorSpaceId == SPACE_DeviceGray)
      m = 1;
    precision = 1;
  }

  HQASSERT(crdInfo->renderTableHasData == !((na == 0) && (nb == 0) && (nc == 0)),
           "Inconsistent value of renderTableHasData");

  if (!crdInfo->renderTableHasData) {
    /* This case is allowed because we wish to have a default render table with no
       actual table but which specifies m=3 output colorants. Thus, rgb colors will be
       preserved on a gray output device. The Adobe way is to output only the red
       component in this case.  */
    NARROW3 (cie, crdInfo->rangeabc);

    /* Note that in B1 we map the range of the above to [0 1]. No idea why not here. */
    for ( i = 0 ; i < m ; ++i ) {
      USERVALUE ftmp = ( USERVALUE )cie[ i ] ;
      NARROW_01( ftmp ) ;
      oColorValues[ i ] = ftmp ;
    }
  }
  else
  {
    register SYSTEMVALUE *rangeabc = crdInfo->rangeabc ;
    int32 offset ;
    int32 oka, okb, okc;     /* whether looking for a next entry in the
                                interpolation table will keep us in bounds */
    SYSTEMVALUE fa, fb, fc;  /* interpolation fractions */
    SYSTEMVALUE table[8];   /* color interpolation table: corners of a cube */

    /* narrow to RangeABC */
    NARROW3 (cie, rangeabc);

    /* compute (a,b,c) */
    cie[0] = (cie[0] - rangeabc[0]) *
      (SYSTEMVALUE)(na - 1) / (rangeabc[1] - rangeabc[0]);
    cie[1] = (cie[1] - rangeabc[2]) *
      (SYSTEMVALUE)(nb - 1) / (rangeabc[3] - rangeabc[2]);
    cie[2] = (cie[2] - rangeabc[4]) *
      (SYSTEMVALUE)(nc - 1) / (rangeabc[5] - rangeabc[4]);

    /* we are required to interpolate. Consider the table as a cube divided
       into cubic cells (like a Rubik cube). The color values are at the
       junctions, but we produce coordinates to anywhere in the cube -
       therefore we are somewhere inside a cell. We interpolate to the eight
       adjoining values as follows: interpolate in one direction along each of
       the parallel four edges, then interpolate between pairs of these results
       in another orthogonal direction to give two values, and finally between
       the remaining two in the third axis to give one value. Job done.


          3 --------------  7
           /|            /|
          / |           / |
         /  |          /  |         a axis: separate strings
        /   |         /   |         b axis: nc * m between consecutive values
       /    |      5 /    |         c axis: m between consecutive values
     1 --------------     |
       |    |       |     |         16bit render table: the number of bytes
       |   2 -------|-----| 6       between consecutive b or c axis entries
       |    /       |    /          is twice the above
     c ^  _/b       |   /
       |  /|        |  /            (a,b,c) = (cie[0], cie[1], cie[2])
       | /          | /
       |/           |/
       ------->------
      0       a     4

    */


    fa = cie[0] - (SYSTEMVALUE) ((int32) cie[0]); /* fraction of way ... */
    fb = cie[1] - (SYSTEMVALUE) ((int32) cie[1]); /* ... into minicube */
    fc = cie[2] - (SYSTEMVALUE) ((int32) cie[2]);
    theo = oArray(theo[3]) + (int32) (cie[0]);
    offset = precision * m * (((int32) (cie[1])) * nc + (int32) (cie[2]));

    oka = cie[0] < na - 1; /* whether there is another minicube 1 index on */
    okb = cie[1] < nb - 1;
    okc = cie[2] < nc - 1;

    b_dist = precision * m * nc;     /* bytes between entries in b */
    c_dist = precision * m;          /* bytes between entries in c */
    diag_dist = b_dist + c_dist;     /* bytes to opp corner of square in b,c */


    for (i = 0; i < m; i++) {
      register uint8 *clist = oString(theo[0]) + offset;

      table[0] = (SYSTEMVALUE) CLIST(0, precision);
      if (okb) {
        table[2] = (SYSTEMVALUE) CLIST(b_dist, precision);
      } else {
        table[2] = table[0];
      }
      if (okc) {
        table[1] = (SYSTEMVALUE) CLIST(c_dist, precision);
        if (okb) {
          table[3] = (SYSTEMVALUE) CLIST(diag_dist, precision);
        } else {
          table[3] = table[1];
        }
      } else {
        table[1] = table[0];
        table[3] = table[2];
      }
      if (oka) {
        clist = oString(theo[1]) + offset;
        table[4] = (SYSTEMVALUE) CLIST(0, precision);
        if (okb) {
          table[6] = (SYSTEMVALUE) CLIST(b_dist, precision);
        } else {
          table[6] = table[4];
        }
        if (okc) {
          table[5] = (SYSTEMVALUE) CLIST(c_dist, precision);
          if (okb) {
            table[7] = (SYSTEMVALUE) CLIST(diag_dist, precision);
          } else {
            table[7] = table[5];
          }
        } else {
          table[5] = table[4];
          table[7] = table[6];
        }
      } else {
        table[4] = table[0];
        table[5] = table[1];
        table[6] = table[2];
        table[7] = table[3];
      }

      /* interpolation in c */
      if (fc > EPSILON) {
        table[0] += fc*(table[1] - table[0]);
        table[2] += fc*(table[3] - table[2]);
        table[4] += fc*(table[5] - table[4]);
        table[6] += fc*(table[7] - table[6]);
      }

      /* interpolation in b */
      if (fb > EPSILON) {
        table[0] += fb*(table[2] - table[0]);
        table[4] += fb*(table[6] - table[4]);
      }

      /* interpolation in a */
      if (fa > EPSILON) {
        table[0] += fa*(table[4] - table[0]);
      }

      arg = (precision == 1 ? table[0] / 255.0 : table[0] / 65535.0);

      /* Call the Rendertable Ti procedures */
      if ((oType(*theproc) != OARRAY && oType(*theproc) != OPACKEDARRAY) ||
           theLen(*theproc) > 0 ) {
        /* N.B. The pre-cached versions of these would currently lose precision
         *      in the case of a 16 bit render table, but we do not expect to
         *      find a non zero length proc in this case.
         */
        HQASSERT(precision == 1, "Unexpected procedure for 16 bit render table");
        if (crdInfo->renderTableData[i].present)
          CALL_C(crdInfo->renderTablefn[i], arg, &crdInfo->renderTableData[i]);
        else if (crdInfo->renderTablefn[i] != NULL)
          CALL_C(crdInfo->renderTablefn[i], arg, NULL);
        else {
          USERVALUE uval = (USERVALUE)arg;

          lookup_callpscache(crdInfo->cpsc_tiproc[i], uval, &uval);
          arg = (SYSTEMVALUE)uval;
        }

        NARROW_01(arg);
      }

      oColorValues[i] = (USERVALUE) arg;
      offset += precision;
      theproc++;
    }
  }

  return TRUE ;
}

#ifdef INVOKEBLOCK_NYI
static Bool crd_invokeBlock( CLINK *pLink , CLINKblock *pBlock )
{
  UNUSED_PARAM(CLINK *, pLink);
  UNUSED_PARAM(CLINKblock *, pBlock);

  crdAssertions(pLink);

  return TRUE;
}
#endif /* INVOKEBLOCK_NYI */


/* crd_scan - scan the CRD section of a CLINK */
static mps_res_t crd_scan( mps_ss_t ss, CLINK *pLink )
{
  return cc_scan_crd( ss, pLink->p.crd->crdInfo );
}


#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the link access functions.
 */
static void crdAssertions(CLINK *pLink)
{
  cc_commonAssertions(pLink,
                      CL_TYPEcrd,
                      0,
                      &CLINKcrd_functions);

  if (pLink->p.crd != NULL)
    crdInfoAssertions(pLink->p.crd->crdInfo);

  switch (pLink->iColorSpace)
  {
  case SPACE_CIEXYZ:
    break;
  default:
    HQFAIL("Bad input color space");
    break;
  }
}
#endif


static void convertXYZtoLab(XYZVALUE cie, XYZVALUE white)
{
/*
 * N.B. This is based on the version in SWcrdcore!src:collib.c, which in turn
 * says the equations are those in following:
 *
 * Color Science: Concepts and Methods, Quantitative
 *                Data and Formulae, Second Edition, (pp166,167)
 *                Wyszecki and Stiles, Pub Wylie 1982.
 *                ISBN 0-471-02106-7
 *
 * Note that there is a change in the function for calculating lab and xyz
 * values at L* = 8, or L_SWITCH.  The ratio of Y to Yn at this point is
 * ~0.008856 or ALPHA_RATIO - it can be found by solving
 * L_SWITCH = 116*ALPHA_RATIO - 16.  The ratio coefficient for L* < L_SWITCH,
 * DELTA_RATIO is ~903.2962962963, and can be found by solving
 * L_SWITCH = DELTA_RATIO*ALPHA_RATIO.  The last value, GAMMA_RATIO falls out
 * as DELTA_RATIO/116.
 *
 * Using symbols for the values in the original equations allows simplification
 * of some expressions to hopefully maintain accuracy.
 */
#define L_SWITCH    (8.0)
#define ALPHA_RATIO (0.008856451679036)     /* Y ratio at which L* = 8 (= (24/116)^3) */
#define DELTA_RATIO (L_SWITCH/ALPHA_RATIO)  /* Which is ~903.2962962963 */
#define GAMMA_RATIO (DELTA_RATIO/116.0)     /* Also L_SWITCH/(116.0*ALPHA_RATIO))
                                               and is ~7.787037037037 */

/* Macros */
#define CUBE_ROOT(x)    pow((x), 1.0/3)
#define XYZ2LABCALC(rInput) (((rInput) > ALPHA_RATIO) ? \
                             (CUBE_ROOT((rInput))) : \
                             (GAMMA_RATIO * (rInput) + (16.0/116.0)))

  double    XDivXn;
  double    YDivYn;
  double    ZDivZn;
  double    fXDivXn;
  double    fYDivYn;
  double    fZDivZn;

  HQASSERT(cie != NULL, "Null cie value in convertXYZtoLab");
  HQASSERT(white != NULL, "Null white pointer in convertXYZtoLab");

  XDivXn = cie[0]/white[0];
  YDivYn = cie[1]/white[1];
  ZDivZn = cie[2]/white[2];

  fXDivXn = XYZ2LABCALC(XDivXn);
  fYDivYn = XYZ2LABCALC(YDivYn);
  fZDivZn = XYZ2LABCALC(ZDivZn);

  if (YDivYn <= ALPHA_RATIO) {
    cie[0] = DELTA_RATIO*YDivYn; /* this now in Lab */
  } else {
    cie[0] = 116.0 * fYDivYn - 16.0; /* this now in Lab */
  }

  cie[1] = 500.0*(fXDivXn - fYDivYn); /* this now in Lab */
  cie[2] = 200.0*(fYDivYn - fZDivZn); /* this now in Lab */
}

/* Sets up the operands that will be passed to the PQR procedure */
static void setupPQR(CLINKcrd *crd)
{
  int32       i;
  SYSTEMVALUE *ws, *bs,    /* C versions of white and black point data */
              *wd, *bd;    /* referred to on page 300 of red book 2 */
  GS_CRDinfo  *crdInfo;

  crdInfo = crd->crdInfo;

  /* Initialise pqr data */
  crd->doPQR = FALSE;
  crd->doSimplePQR = FALSE;
  crd->Ws = onothing ; /* Struct copy to set slot properties */
  crd->Bs = onothing ; /* Struct copy to set slot properties */
  crd->Wd = onothing ; /* Struct copy to set slot properties */
  crd->Bd = onothing ; /* Struct copy to set slot properties */
  for (i = 0; i < 24; i++)
    object_store_real(object_slot_notvm(&crd->tristimulus[i]), 0.0);

  /* Early bail-out for some usages where neutral mapping is done in it's own link */
  if (!crd->neutralMappingNeeded)
    return;

  HQASSERT(IS_INTERPRETER(), "Running CRD PQR procedures at the back end");

  /* set up defaults for white and black points for the PQR transforms */
  ws = crd->sourceWP;
  bs = crd->sourceBP;
  wd = crdInfo->whitepoint;
  bd = crdInfo->blackpoint;

  /* see if we need to do anything at all by comparing input and output
   * white and black points */
  if (ws[0] != wd[0] || ws[1] != wd[1] || ws[2] != wd[2] ||
      bs[0] != bd[0] || bs[1] != bd[1] || bs[2] != bd[2]) {
    crd->doPQR = TRUE;
    crd->doSimplePQR = TRUE;

    if (fabs(ws[0] - wd[0]) > XYZ_EPSILON ||
        fabs(ws[1] - wd[1]) > XYZ_EPSILON ||
        fabs(ws[2] - wd[2]) > XYZ_EPSILON ||
        fabs(bs[0] - bd[0]) > XYZ_EPSILON ||
        fabs(bs[1] - bd[1]) > XYZ_EPSILON ||
        fabs(bs[2] - bd[2]) > XYZ_EPSILON) {
      OBJECT *theo;

      crd->doSimplePQR = FALSE;

      for ( i = 0 ; i < 3 ; i++ )
        theo = crdInfo->transformpqr + i ;

      /* Transform according to procedures TransformPQR.
         These procedures are required in the dictionary, but if no dictionary
         was supplied it would be useful to have a default algorithm (and it
         would go faster in C)
         First, compute the Ws etc arrays, the operands to TransformPQR: these
         are the vast formulae on pages 300 to 301 of the Red Book 2 */

      theTags(crd->Ws) = OARRAY | LITERAL | READ_ONLY;
      theMark(crd->Ws) = ISLOCAL | ISNOTVM | SAVEMASK ;
      theLen(crd->Ws) = 6;
      oArray(crd->Ws) = crd->tristimulus;
      theTags(crd->Bs) = OARRAY | LITERAL | READ_ONLY;
      theMark(crd->Bs) = ISLOCAL | ISNOTVM | SAVEMASK ;
      theLen(crd->Bs) = 6;
      oArray(crd->Bs) = crd->tristimulus + 6;
      theTags(crd->Wd) = OARRAY | LITERAL | READ_ONLY;
      theMark(crd->Wd) = ISLOCAL | ISNOTVM | SAVEMASK ;
      theLen(crd->Wd) = 6;
      oArray(crd->Wd) = crd->tristimulus + 12;
      theTags(crd->Bd) = OARRAY | LITERAL | READ_ONLY;
      theMark(crd->Bd) = ISLOCAL | ISNOTVM | SAVEMASK ;
      theLen(crd->Bd) = 6;
      oArray(crd->Bd) = crd->tristimulus + 18;

      for ( i = 0 ; i < 3 ; i++ ) {
        object_store_real(object_slot_notvm(&crd->tristimulus[i]),
                          (USERVALUE)ws[i]) ;
        object_store_real(object_slot_notvm(&crd->tristimulus[6 + i]),
                          (USERVALUE)bs[i]) ;
        object_store_real(object_slot_notvm(&crd->tristimulus[12 + i]),
                          (USERVALUE)wd[i]) ;
        object_store_real(object_slot_notvm(&crd->tristimulus[18 + i]),
                          (USERVALUE)bd[i]) ;
        if (crdInfo->matrixpqrPresent) {
          SYSTEMVALUE mpqr0 = crdInfo->matrixpqr[i] ;
          SYSTEMVALUE mpqr3 = crdInfo->matrixpqr[i + 3] ;
          SYSTEMVALUE mpqr6 = crdInfo->matrixpqr[i + 6] ;

          object_store_real(object_slot_notvm(&crd->tristimulus[3 + i]),
                            (USERVALUE)(ws[0] * mpqr0 + ws[1] * mpqr3 + ws[2] * mpqr6)) ;
          object_store_real(object_slot_notvm(&crd->tristimulus[9 + i]),
                            (USERVALUE)(bs[0] * mpqr0 + bs[1] * mpqr3 + bs[2] * mpqr6)) ;
          object_store_real(object_slot_notvm(&crd->tristimulus[15 + i]),
                            (USERVALUE)(wd[0] * mpqr0 + wd[1] * mpqr3 + wd[2] * mpqr6)) ;
          object_store_real(object_slot_notvm(&crd->tristimulus[21 + i]),
                            (USERVALUE)(bd[0] * mpqr0 + bd[1] * mpqr3 + bd[2] * mpqr6)) ;
        } else {
          object_store_real(object_slot_notvm(&crd->tristimulus[3 + i]),
                            (USERVALUE)ws[i]);
          object_store_real(object_slot_notvm(&crd->tristimulus[9 + i]),
                            (USERVALUE)bs[i]);
          object_store_real(object_slot_notvm(&crd->tristimulus[15 + i]),
                            (USERVALUE)wd[i]);
          object_store_real(object_slot_notvm(&crd->tristimulus[21 + i]),
                            (USERVALUE)bd[i]);
        }
      }
    }
  }
}

/* Executes the PQR procedure */
static Bool doPQR(XYZVALUE cie, CLINKcrd *crd, GS_CRDinfo  *crdInfo)
{
  int32 i;

  if (crd->doSimplePQR) {
    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++) {
      cie[i] *= (crdInfo->whitepoint[i] / crd->sourceWP[i]);
    }
  }
  else {
    /* multiply by MatrixPQR */
    if (crdInfo->matrixpqrPresent) {
      MATRIX_MULTIPLY (cie, crdInfo->matrixpqr);
    }

    /* narrow to RangePQR */
    NARROW3 (cie, crdInfo->rangepqr);

    /* Transform according to procedures TransformPQR.
       These procedures are required in the dictionary, but if no dictionary
       was supplied it would be useful to have a default algorithm (and it
       would go faster in C)
       First, compute the Ws etc arrays, the operands to TransformPQR: these
       are the vast formulae on pages 300 to 301 of the Red Book 2 */

    for (i = 0; i < NUMBER_XYZ_COMPONENTS; i++) {
      if (crdInfo->transformpqrfn[i] != NULL)
        CALL_C(crdInfo->transformpqrfn[i], cie[i], crd->tristimulus);
      else {
        /* Would like to cache these calls to the interpreter, but cannot
         * as 4 args are not known early enough. So have to disable MTC to
         * allow interpreter calls to go ahead.
         */
        if ( !push4(&crd->Ws, &crd->Bs, &crd->Wd, &crd->Bd, &operandstack) )
          return FALSE;
        if ( !crd_callps(crdInfo->transformpqr + i, &cie[i]) )
          return FALSE;
      }
    }

    /* now by the inverse matrix of PQR */
    if (crdInfo->matrixpqrPresent) {
      MATRIX_MULTIPLY (cie, crdInfo->matrixpqrinv);
    }
  }

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/*
 * CRD Info Data Access Functions
 * ==============================
 */

static Bool cc_createcrdinfo( GS_CRDinfo **crdInfo,
                              int32      nDimensions )
{
  int32               i;
  GS_CRDinfo          *pInfo;
  size_t              structSize;

  HQASSERT(nDimensions >= 0, "Low value of crd dimension");
  HQASSERT(nDimensions <= 20, "Unreasonably high value of crd dimension");

  structSize = sizeof(GS_CRDinfo);

  *crdInfo = NULL;

  pInfo = mm_sac_alloc(mm_pool_color,
                       structSize,
                       MM_ALLOC_CLASS_NCOLOR);
  if (pInfo == NULL)
    return error_handler(VMERROR);

  pInfo->renderTablefn = NULL;
  pInfo->renderTableData = NULL;
  if (nDimensions > 0) {
    pInfo->renderTablefn = mm_sac_alloc(mm_pool_color,
                                        nDimensions * sizeof(CIECallBack),
                                        MM_ALLOC_CLASS_NCOLOR);
    pInfo->renderTableData = mm_sac_alloc(mm_pool_color,
                                          nDimensions * sizeof(RENDERTABLE_DATA),
                                          MM_ALLOC_CLASS_NCOLOR);
    if (pInfo->renderTablefn == NULL || pInfo->renderTableData == NULL) {
      cc_destroycrdinfo(&pInfo);
      return error_handler(VMERROR);
    }
  }

  *crdInfo = pInfo;

  pInfo->refCnt     = 1 ;
  pInfo->structSize = structSize;
  pInfo->dimension  = nDimensions ;
  pInfo->convertXYZtoLabMethod     = CONVERT_XYZ_NONE ;
  pInfo->encodeabcPresent          = FALSE ;
  pInfo->matrixabcPresent          = FALSE ;
  pInfo->encodelmnPresent          = FALSE ;
  pInfo->matrixlmnPresent          = FALSE ;
  pInfo->matrixpqrPresent          = FALSE ;
  pInfo->renderTablePresent        = FALSE ;
  pInfo->renderTableHasData        = FALSE;
  pInfo->outputColorSpaceId        = SPACE_notset;
  pInfo->colorrendering = onull ; /* Struct copy to set slot properties */
  pInfo->outputColorSpace = onull; /* Struct copy to set slot properties */

  for ( i = 0 ; i < 3 ; i++ ) {
    pInfo->encodeabc[ i ] = onull ; /* Struct copy to set slot properties */
    pInfo->encodeabcData[ i ].present = FALSE ;
    pInfo->encodeabcData[ i ].mul = 0.0 ;
    pInfo->encodeabcData[ i ].sub = 0.0 ;
    pInfo->encodeabcData[ i ].div = 0.0 ;
    pInfo->encodeabcData[ i ].array = onull ; /* Struct copy to set slot properties */
    pInfo->encodeabcfn[ i ] = NULL ;

    pInfo->encodelmn[ i ] = onull ; /* Struct copy to set slot properties */
    pInfo->encodelmnData[ i ].present = FALSE ;
    pInfo->encodelmnData[ i ].div = 0.0 ;
    pInfo->encodelmnData[ i ].le = 0.0 ;
    pInfo->encodelmnData[ i ].mul = 0.0 ;
    pInfo->encodelmnfn[ i ] = NULL ;

    pInfo->transformpqr[ i ] = onull ; /* Struct copy to set slot properties */
    pInfo->transformpqrfn[ i ] = NULL ;

    pInfo->cpsc_encodelmn[i]    = NULL;
    pInfo->cpsc_encodeabc[i]    = NULL;
    pInfo->cpsc_tiproc[i]       = NULL;
  }
  pInfo->cpsc_tiproc[3] = NULL; /* other are 0,1,2, this one has 3 too */

  pInfo->renderTable16bit = FALSE;
  pInfo->rendertable = onull; /* Struct copy to set slot properties */
  for ( i = 0 ; i < pInfo->dimension ; i++ ) {
    pInfo->renderTablefn[ i ] = NULL ;
    pInfo->renderTableData[ i ].present = FALSE ;
    pInfo->renderTableData[ i ].div = 0.0 ;
    pInfo->renderTableData[ i ].array = onull ; /* Struct copy to set slot properties */
  }

  pInfo->nXUIDs = 0;
  pInfo->pXUIDs = NULL;

  crdInfoAssertions(pInfo);

  return TRUE;
}

static void freecrdinfo( GS_CRDinfo *crdInfo )
{
  int32 i;

  if (crdInfo->renderTablefn != NULL)
    mm_sac_free(mm_pool_color,
                crdInfo->renderTablefn,
                crdInfo->dimension * sizeof(CIECallBack));
  if (crdInfo->renderTableData != NULL)
    mm_sac_free(mm_pool_color,
                crdInfo->renderTableData,
                crdInfo->dimension * sizeof(RENDERTABLE_DATA));

  cc_destroy_xuids(&crdInfo->nXUIDs, &crdInfo->pXUIDs);

  for ( i = 0 ; i < 3 ; i++ ) {
    if ( crdInfo->cpsc_encodelmn[i] != NULL )
      destroy_callpscache(&crdInfo->cpsc_encodelmn[i]);
    if ( crdInfo->cpsc_encodeabc[i] != NULL )
      destroy_callpscache(&crdInfo->cpsc_encodeabc[i]);
    if ( crdInfo->cpsc_tiproc[i] != NULL )
      destroy_callpscache(&crdInfo->cpsc_tiproc[i]);
  }
  if ( crdInfo->cpsc_tiproc[3] != NULL ) /* this array has one extra entry */
    destroy_callpscache(&crdInfo->cpsc_tiproc[3]);
  mm_sac_free(mm_pool_color, crdInfo, crdInfo->structSize);
}

void cc_destroycrdinfo( GS_CRDinfo **crdInfo )
{
  if ( *crdInfo != NULL ) {
    crdInfoAssertions(*crdInfo);
    CLINK_RELEASE(crdInfo, freecrdinfo);
  }
}

void cc_reservecrdinfo( GS_CRDinfo *crdInfo )
{
  if ( crdInfo != NULL ) {
    crdInfoAssertions( crdInfo );
    CLINK_RESERVE( crdInfo ) ;
  }
}

Bool cc_arecrdobjectslocal(corecontext_t *corecontext, GS_CRDinfo *crdInfo )
{
  int i;

  if ( crdInfo == NULL )
    return FALSE ;

  if ( illegalLocalIntoGlobal(&crdInfo->colorrendering, corecontext) )
    return TRUE ;

  for ( i = 0 ; i < 3 ; i++ ) {
    if ( crdInfo->encodeabcPresent
         && illegalLocalIntoGlobal(&crdInfo->encodeabc[i], corecontext) )
      return TRUE ;
    if ( crdInfo->encodelmnPresent
         && illegalLocalIntoGlobal(&crdInfo->encodelmn[i], corecontext) )
      return TRUE ;
    if ( illegalLocalIntoGlobal(&crdInfo->transformpqr[i], corecontext) )
      return TRUE ;
  }
  if ( illegalLocalIntoGlobal(&crdInfo->rendertable, corecontext) )
    return TRUE ;
  /* no need to check outputColorSpace because that is also
     part of rendertable */

  return FALSE ;
}


/* scan_crd - scan a GS_CRDinfo */
mps_res_t cc_scan_crd( mps_ss_t ss, GS_CRDinfo *crdInfo )
{
  size_t i;
  mps_res_t res;

  if ( crdInfo == NULL )
    return MPS_RES_OK;

  res = ps_scan_field( ss, &crdInfo->colorrendering );
  if ( res != MPS_RES_OK ) return res;
  for ( i = 0 ; i < 3 ; i++ ) {
    if ( crdInfo->encodeabcPresent ) {
      res = ps_scan_field( ss, &crdInfo->encodeabc[ i ]);
      if ( res != MPS_RES_OK ) return res;
    }
    if ( crdInfo->encodelmnPresent ) {
      res = ps_scan_field( ss, &crdInfo->encodelmn[ i ]);
      if ( res != MPS_RES_OK ) return res;
    }
    res = ps_scan_field( ss, &crdInfo->transformpqr[ i ]);
    if ( res != MPS_RES_OK ) return res;
  }
  res = ps_scan_field( ss, &crdInfo->rendertable );
  /* No need to scan outputColorSpace as it is also part of
   * rendertable. */
  return res;
}


#if defined( ASSERT_BUILD )
/*
 * A list of assertions common to all the transfer info access functions.
 */
static void crdInfoAssertions(GS_CRDinfo *pInfo)
{
  HQASSERT(pInfo != NULL, "pInfo not set");
  HQASSERT(pInfo->structSize == sizeof(GS_CRDinfo),
           "structure size not correct");
  HQASSERT(pInfo->refCnt > 0, "refCnt not valid");
 }
#endif


/* ----------------------------------------------------------------------- */

static GS_CRDinfo *cc_getCrdInfo(GS_COLORinfo *colorInfo)
{
  if ( colorInfo->crdInfo == NULL ) {
    if ( ! cc_createcrdinfo( &colorInfo->crdInfo , 0 ))
      return FAILURE(NULL);
  }
  return colorInfo->crdInfo ;
}

void cc_crd_details(GS_CRDinfo    *crdInfo,
                    int32         *outputDimensions,
                    COLORSPACE_ID *outputSpaceId)
{
  HQASSERT(crdInfo != NULL, "crdInfo NULL");
  HQASSERT(outputDimensions != NULL, "outputDimensions NULL");
  HQASSERT(outputSpaceId != NULL, "outputSpaceId NULL");

  *outputDimensions = crdInfo->dimension;
  *outputSpaceId = crdInfo->outputColorSpaceId;
}

/* ----------------------------------------------------------------------- */

Bool gsc_setcolorrendering(GS_COLORinfo *colorInfo, STACK *stack)
{
  /* Sets up a CIE color rendering dictionary in the graphics state -
   * applied when converting from CIE to device color.
   */
  GS_CRDinfo *crdInfo ;
  GS_CRDinfo *newcrdInfo = NULL;
  OBJECT *theo ;
  OBJECT *thed ;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");

  /* Make sure there is a crdInfo associated with the current gstate */
  crdInfo = cc_getCrdInfo(colorInfo) ;
  if (crdInfo == NULL)
    return FALSE;

  if ( isEmpty( *stack ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = theTop( *stack ) ;

  if ( oType(*theo) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  thed = oDict(*theo) ;
  if ( ! oCanRead(*thed) && !object_access_override(thed) )
    return error_handler( INVALIDACCESS ) ;

  if ( !OBJECTS_IDENTICAL(crdInfo->colorrendering, *theo) ) {
    if ( ! cc_setcolorrendering( colorInfo, &newcrdInfo, theo )) {
      if ( newcrdInfo != NULL )
        cc_destroycrdinfo( &newcrdInfo ) ;
      return FALSE ;
    }
  }

  /* newcrdInfo should only be NULL when we want to keep the old 'cause new == old. */
  if ( newcrdInfo != NULL ) {
    colorInfo->crdInfo = newcrdInfo ;

    if ( ! cc_invalidateColorChains( colorInfo, TRUE ))
      return FALSE ;

    /* do this after freeing the chains, since a chain probably references the crd */
    cc_destroycrdinfo( & crdInfo ) ;
  }

  pop( stack ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */

/* The RB3 only defines a Type 1 crd. We also have a non-standard Type 2 crd to
 * assist findcolorrendering. That operator is defined to return a crd, which
 * would normally be the operand to a later call to setcolorrendering. In a
 * colour environment based on ICC profiles, this is an irritation because we
 * would be converting an ICC table to a postscript crd with the inherit quality
 * problems of the connection space being CIEXYZ and limited by an 8 bit RenderTable.
 * Instead, fincolorrendering returns a Type 2 crd that contains only a trampoline
 * onto setrenderingintent (which is also an Hqn extension).
 */
Bool cc_setcolorrendering( GS_COLORinfo *colorInfo,
                           GS_CRDinfo **pcrdInfo,
                           OBJECT *crdObject )
{
  enum {crd_ColorRenderingType};
  static NAMETYPEMATCH thematch[] = {
    { NAME_ColorRenderingType,             1, { OINTEGER }},
    DUMMY_END_MATCH
  };

  OBJECT *theo ;

  if (!dictmatch(crdObject, thematch))
    return FALSE ;

  theo = thematch[crd_ColorRenderingType].result;
  if (theo != NULL) {
    if (oInteger(*theo) == 1) {
      if (!cc_setcolorrendering_1(colorInfo, pcrdInfo, crdObject))
        return FALSE;
    }
    else if (oInteger(*theo) == 2) {
      if (!cc_setcolorrendering_2(colorInfo, crdObject))
        return FALSE;
    }
    else
      return error_handler( RANGECHECK ) ;

  }

  return TRUE;
}

static CALLPSCACHE *crd_createcache(int32 fnType, OBJECT *psobj,
                                    SYSTEMVALUE *crd_range)
{
  CALLPSCACHE *cpsc;

  if ( (cpsc = create_callpscache(fnType, 1, 0, crd_range, psobj)) == NULL )
    return NULL;

  return cpsc;
}

/**
 * pre-cache the results of calling the various bits of PS
 * present in the CRD.
 */
static Bool crd_cache_ps_calls(GS_CRDinfo *crdInfo)
{
  CALLPSCACHE *cpsc;
  OBJECT *theproc;
  int32 i;

  /* call the EncodeLMN procedures */
  if ( crdInfo->encodelmnPresent ) {
    for (i = 0; i < 3; i++) {
      if ( !crdInfo->encodelmnData[i].present &&
           crdInfo->encodelmnfn[i] == NULL ) {
        /*
         * CRD EncodeLMN function does not have a officialy specified range,
         * rather is limited to XYZ CIE values, which usually run between
         * -0.1 and 1.1, but more extreme values are theoretcially possible.
         * So need to have a somewhat heuristic range : Use -1.0 -> 2.0 until
         * it is proved that this is not sufficient.
         */
        SYSTEMVALUE range[2] = { -1.0, 2.0 };

        theproc = crdInfo->encodelmn + i;
        if ( (cpsc = crd_createcache(FN_CRD_LMNFUNC, theproc, range)) == NULL )
          return FALSE;
        crdInfo->cpsc_encodelmn[i] = cpsc;
      }
    }
  }

  /* call the EncodeABC procedures */
  if (crdInfo->encodeabcPresent) {
    for (i = 0; i < 3; i++) {
      if ( !crdInfo->encodeabcData[i].present &&
            crdInfo->encodeabcfn[i] == NULL ) {
        theproc = crdInfo->encodeabc+i;
        if ( (cpsc = crd_createcache(FN_CRD_ABCFUNC, theproc,
                                     &(crdInfo->rangeabc[2*i]))) == NULL )
          return FALSE;
        crdInfo->cpsc_encodeabc[i] = cpsc;
      }
    }
  }

  /* Call the RenderTable Ti procedures */
  if ( crdInfo->renderTablePresent ) {
    for (i = 0; i < crdInfo->dimension; i++) {
      if ( !crdInfo->renderTableData[i].present &&
            crdInfo->renderTablefn[i] == NULL ) {
           /* Ti inputs are lookups from rendertable strings divided by 255,
            * so must be in the range [0,1]
           */
          theproc = oArray(crdInfo->rendertable) + 5 + i;
          if ((oType(*theproc) != OARRAY && oType(*theproc) != OPACKEDARRAY) ||
               theLen(*theproc) > 0 ) {
            if ( (cpsc = crd_createcache(FN_CRD_TIFUNC, theproc,
                                         NULL)) == NULL )
              return FALSE;
            crdInfo->cpsc_tiproc[i] = cpsc;
          }
        }
      }
    }

  return TRUE;
}

static Bool cc_setcolorrendering_1( GS_COLORinfo  *colorInfo,
                                    GS_CRDinfo    **pcrdInfo,
                                    OBJECT        *crdObject )
{
  enum {crd_MatrixLMN, crd_EncodeLMN, crd_RangeLMN, crd_MatrixABC, crd_EncodeABC,
        crd_RangeABC, crd_ConvertXYZtoLab, crd_WhitePoint, crd_BlackPoint,
        crd_RelativeWhitePoint, crd_RelativeBlackPoint, crd_MatrixPQR,
        crd_RangePQR, crd_TransformPQR, crd_RenderTable16Bit, crd_RenderTable,
        crd_UniqueID, crd_XUID};
  static NAMETYPEMATCH thematch[] = {
    { NAME_MatrixLMN | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }},
    { NAME_EncodeLMN | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }},
    { NAME_RangeLMN | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},
    { NAME_MatrixABC | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }},
    { NAME_EncodeABC | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }},
    { NAME_RangeABC | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},
    { NAME_ConvertXYZtoLab | OOPTIONAL,    2, { ONAME, OBOOLEAN }},
    { NAME_WhitePoint,                     2, { OARRAY, OPACKEDARRAY }},
    { NAME_BlackPoint | OOPTIONAL,         2, { OARRAY, OPACKEDARRAY }},
    { NAME_RelativeWhitePoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
    { NAME_RelativeBlackPoint | OOPTIONAL, 2, { OARRAY, OPACKEDARRAY }},
    { NAME_MatrixPQR | OOPTIONAL,          2, { OARRAY, OPACKEDARRAY }},
    { NAME_RangePQR | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},
    { NAME_TransformPQR,                   2, { OARRAY, OPACKEDARRAY }},
    { NAME_RenderTable16Bit | OOPTIONAL,   1, { OBOOLEAN }},
    { NAME_RenderTable | OOPTIONAL,        2, { OARRAY, OPACKEDARRAY }},

    { NAME_UniqueID | OOPTIONAL,           1, { OINTEGER }},
    { NAME_XUID     | OOPTIONAL,           2, { OARRAY, OPACKEDARRAY }},

    DUMMY_END_MATCH
  } ;

  int32 i ;
  int32 j ;
  OBJECT *theo ;
  OBJECT *olist ;
  OBJECT outputColorSpace = OBJECT_NOTVM_NOTHING;
  COLORSPACE_ID outputColorSpaceId;
  int32 m , na , nb , nc ;
  GS_CRDinfo *crdInfo ;

  if ( ! dictmatch( crdObject , thematch ))
    return FALSE ;

  /* Decide how many dimensions are on the output of the crd from the RenderTable */
  theo = thematch[crd_RenderTable].result ;
  if ( theo ) {
    /* 6 is the minimum, (e.g. for DeviceGray we will have only one procedure
       element), and allows us, for example, to access m at element 4, but once
       we know about m we can check this length more completely */
    if ( theLen(*theo) < 6 )
      return error_handler( TYPECHECK ) ;
    olist = oArray(*theo) ;
    /* harlequin extension: either m is a number as per the red book, or is a color
       space (a name or an array) which provides the value of m implictly, plus (in
       the case of a DeviceN for example, the tint transofrm and alternate space to
       divert to some other space, and the names of colorants represented in the CRD
       output */
    switch (oType(olist[ 4 ])) {
    case ONAME:
    case OARRAY:
    case OPACKEDARRAY:
      if (! gsc_getcolorspacesizeandtype(colorInfo, &olist[ 4 ],
                                         &outputColorSpaceId, &m))
        return FALSE;
      Copy(& outputColorSpace, & olist[4]);
      break;
    case OINTEGER:
      m = oInteger( olist[ 4 ]) ;
      switch (m) {
      case 1:
        object_store_name(&outputColorSpace, NAME_DeviceGray, LITERAL);
        outputColorSpaceId = SPACE_DeviceGray;
        break;
      case 3:
        object_store_name(&outputColorSpace, NAME_DeviceRGB, LITERAL);
        outputColorSpaceId = SPACE_DeviceRGB;
        break;
      case 4:
        object_store_name(&outputColorSpace, NAME_DeviceCMYK, LITERAL);
        outputColorSpaceId = SPACE_DeviceCMYK;
        break;
      default:
        return error_handler( RANGECHECK ) ;
      }
      break;
    default:
      return error_handler( TYPECHECK ) ;
    }
    if ( theLen(*theo) != m + 5 )
      return error_handler( RANGECHECK ) ;
  }
  else
  {
    /* default for when no table is present: may be overridden if we output to DeviceGray */
    m = 3;
    object_store_name(&outputColorSpace, NAME_DeviceRGB, LITERAL);
    outputColorSpaceId = SPACE_DeviceRGB;
  }

  /* Make sure there is a crdInfo; always use a new one.
   * NB. crdInfo->dimension -> m
   */
  if ( ! cc_createcrdinfo( pcrdInfo , m ))
    return FALSE ;
  crdInfo = *pcrdInfo ;

  /* Keep a copy in the graphics state and extract the relevant fields */
  Copy( & crdInfo->colorrendering , crdObject ) ;

  /* assume none present until we see them */
  crdInfo->convertXYZtoLabMethod     = CONVERT_XYZ_NONE ;
  crdInfo->encodeabcPresent          = FALSE ;
  crdInfo->matrixabcPresent          = FALSE ;
  crdInfo->encodelmnPresent          = FALSE ;
  crdInfo->matrixlmnPresent          = FALSE ;
  crdInfo->matrixpqrPresent          = FALSE ;
  crdInfo->renderTablePresent        = FALSE ;
  crdInfo->renderTableHasData        = FALSE ;

  HQASSERT(crdInfo->dimension == m, "CRD dimension is incorrect"); ;

  crdInfo->outputColorSpaceId = outputColorSpaceId;
  Copy(& crdInfo->outputColorSpace, & outputColorSpace);

  theo = thematch[ crd_MatrixLMN ].result;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->matrixlmn, 9))
      return FALSE;
    if (!identityMatrix(crdInfo->matrixlmn))
      crdInfo->matrixlmnPresent = TRUE ;
  }

  theo = thematch[crd_EncodeLMN].result ;
  if ( theo != NULL) {
    if (!cc_extractP( crdInfo->encodelmn , 3 , theo ))
      return FALSE;
    crdInfo->encodelmnPresent = TRUE ;

    for (i = 0; i < 3; i++) {
      if (theLen(crdInfo->encodelmn[i]) != 0) {
        crdInfo->encodelmnfn[i] = findEncodelmnProc(&crdInfo->encodelmn[i],
                                                    &crdInfo->encodelmnData[i]);
        if (crdInfo->encodelmnfn[i] == NULL)
          crdInfo->encodelmnfn[i] = cc_findCIEProcedure(&crdInfo->encodelmn[i]);

        if (crdInfo->encodelmnfn[i] == NULL)
          HQTRACE(crdPSCallbackTrace, ("Executing encodelmn PS"));
      }
    }
  }

  theo = thematch[crd_RangeLMN].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->rangelmn, 6))
      return FALSE;
  }
  else {
    crdInfo->rangelmn[ 0 ] =
      crdInfo->rangelmn[ 2 ] =
        crdInfo->rangelmn[ 4 ] = 0.0 ;
    crdInfo->rangelmn[ 1 ] =
      crdInfo->rangelmn[ 3 ] =
        crdInfo->rangelmn[ 5 ] = 1.0 ;
  }

  theo = thematch[crd_MatrixABC].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->matrixabc, 9))
      return FALSE;
    if (!identityMatrix(crdInfo->matrixabc))
      crdInfo->matrixabcPresent = TRUE ;
  }

  theo = thematch[crd_EncodeABC].result ;
  if ( theo != NULL ) {
    if (!cc_extractP( crdInfo->encodeabc , 3 , theo ))
      return FALSE;
    crdInfo->encodeabcPresent = TRUE ;

    for (i = 0; i < 3; i++) {
      if (theLen(crdInfo->encodeabc[i]) != 0) {
        crdInfo->encodeabcfn[i] = findEncodeabcProc(&crdInfo->encodeabc[i],
                                                    &crdInfo->encodeabcData[i]);
        if (crdInfo->encodeabcfn[i] == NULL)
          crdInfo->encodeabcfn[i] = cc_findCIEProcedure(&crdInfo->encodeabc[i]);

        if (crdInfo->encodeabcfn[i] == NULL)
          HQTRACE(crdPSCallbackTrace, ("Executing encodeabc PS"));
      }
    }
  }

  theo = thematch[crd_RangeABC].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->rangeabc, 6))
      return FALSE;
  }
  else {
    crdInfo->rangeabc[ 0 ] =
      crdInfo->rangeabc[ 2 ] =
        crdInfo->rangeabc[ 4 ] = 0.0 ;
    crdInfo->rangeabc[ 1 ] =
      crdInfo->rangeabc[ 3 ] =
        crdInfo->rangeabc[ 5 ] = 1.0 ;
  }

  theo = thematch[crd_ConvertXYZtoLab].result ;
  if ( theo != NULL ) {
    if (oType(*theo) == ONAME) {
      if (oNameNumber(*theo) == NAME_RelativeWhitePoint)
        crdInfo->convertXYZtoLabMethod = CONVERT_XYZ_RELATIVE_WP;
      else if (oNameNumber(*theo) == NAME_WhitePoint)
        crdInfo->convertXYZtoLabMethod = CONVERT_XYZ_WP;
      else
        return error_handler(RANGECHECK);
    }
    else if (oBool(*theo)) {
      /* This is a legacy method that we have to support */
      crdInfo->convertXYZtoLabMethod = CONVERT_XYZ_RELATIVE_WP;
    }
  }

  theo = thematch[crd_WhitePoint].result ; /* required */
  if (!object_get_numeric_array(theo, crdInfo->whitepoint, 3))
    return FALSE;
  for ( i = 0 ; i < 3 ; ++i ) {
    if (crdInfo->whitepoint[i] <= 0)
      return detail_error_handler(RANGECHECK, "Invalid WhitePoint in CRD.");
  }

  theo = thematch[crd_BlackPoint].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->blackpoint, 3))
      return FALSE;
  }
  else {
    for ( i = 0 ; i < 3 ; ++i )
      crdInfo->blackpoint[i] = 0;
  }

  theo = thematch[crd_RelativeWhitePoint].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->relativewhitepoint, 3))
      return FALSE;
    for ( i = 0 ; i < 3 ; ++i ) {
      if (crdInfo->relativewhitepoint[i] <= 0)
        return detail_error_handler(RANGECHECK, "Invalid RelativeWhitePoint in CRD.");
    }
  }
  else {
    for ( i = 0 ; i < 3 ; ++i )
      crdInfo->relativewhitepoint[i] = crdInfo->whitepoint[i];
  }

  theo = thematch[crd_RelativeBlackPoint].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->relativeblackpoint, 3))
      return FALSE;
  }
  else {
    for ( i = 0 ; i < 3 ; ++i )
      crdInfo->relativeblackpoint[i] = crdInfo->blackpoint[i];
  }

  theo = thematch[crd_MatrixPQR].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->matrixpqr, 9))
      return FALSE;
    if (!identityMatrix(crdInfo->matrixpqr)) {
      crdInfo->matrixpqrPresent = TRUE ;

      /* Invert the 3x3 matrix MatrixPQR */
      if (!matrix_inverse_3x3(crdInfo->matrixpqr, crdInfo->matrixpqrinv))
        return error_handler( LIMITCHECK ) ;
    }
  }

  theo = thematch[crd_RangePQR].result ;
  if ( theo != NULL ) {
    if (!object_get_numeric_array(theo, crdInfo->rangepqr, 6))
      return FALSE;
  }
  else {
    crdInfo->rangepqr[ 0 ] = -0.09;
    crdInfo->rangepqr[ 1 ] =  2.90;
    crdInfo->rangepqr[ 2 ] = -0.03;
    crdInfo->rangepqr[ 3 ] =  2.50;
    crdInfo->rangepqr[ 4 ] = -0.13;
    crdInfo->rangepqr[ 5 ] =  6.70;
  }

  theo = thematch[crd_TransformPQR].result ; /* required */
  if (!cc_extractP( crdInfo->transformpqr , 3 , theo ))
    return FALSE;

  for (i = 0; i < 3; i++) {
    if (theLen(crdInfo->transformpqr[i]) != 0) {
      crdInfo->transformpqrfn[i] = cc_findCIEProcedure(&crdInfo->transformpqr[i]);

      if (crdInfo->transformpqrfn[i] == NULL)
        HQTRACE(crdPSCallbackTrace, ("Executing transformpqr PS"));
    }
  }

  theo = thematch[crd_RenderTable16Bit].result ;
  if ( theo != NULL ) {
    if (oBool(*theo)) {
      crdInfo->renderTable16bit = TRUE;
    }
  }

  theo = thematch[crd_RenderTable].result ;
  na = nb = nc = 0;
  if ( theo != NULL ) {
    /* We know how big crdInfo->dimension is from above, and have checked it */
    olist = oArray( *theo ) ;
    if ( oType( olist[ 0 ]) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    na = oInteger( olist[ 0 ]) ;
    if ( oType( olist[ 1 ]) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    nb = oInteger( olist[ 1 ]) ;
    if ( oType( olist[ 2 ]) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    nc = oInteger( olist[ 2 ]) ;

    if ( !(na == 0 && nb == 0 && nc == 0))
      crdInfo->renderTableHasData = TRUE;

    /* The last crdInfo->dimension elements of theo should be procedures. */
    for ( i = 5, j = 0 ; i < 5 + crdInfo->dimension ; ++i, ++j ) {

      if (( oType(olist[i]) != OARRAY && oType(olist[i]) != OPACKEDARRAY ) ||
          ! oExecutable(olist[i]))
        return error_handler( TYPECHECK ) ;
      if ( ! oCanExec(olist[i]) && !object_access_override(&olist[i]) )
        return error_handler( INVALIDACCESS ) ;

      if (theLen(olist[i]) != 0) {
        crdInfo->renderTablefn[j] = findRenderTableProc(&olist[i], &crdInfo->renderTableData[j]);
        if (crdInfo->renderTablefn[j] == NULL)
          crdInfo->renderTablefn[j] = cc_findCIEProcedure(&olist[i]);

        if (crdInfo->renderTablefn[j] == NULL)
          HQTRACE(crdPSCallbackTrace, ("Executing rendertable PS"));
      }
    }

    /* That just leaves the fourth element, which must be an array of strings
     * of particular sizes.
     */
    if ( oType(olist[3]) != OARRAY && oType(olist[3]) != OPACKEDARRAY)
      return error_handler( TYPECHECK ) ;
    if ( ! oCanRead(olist[3]) && !object_access_override(&olist[3]) )
      return error_handler( INVALIDACCESS ) ;
    if ( theLen( olist[ 3 ]) != na )
      return error_handler( RANGECHECK ) ;

    olist = oArray( olist[ 3 ]) ;
    for ( i = 0 ; i < na ; ++i ) {
      if ( oType(olist[i]) != OSTRING )
        return error_handler( TYPECHECK ) ;
      if ( ! oCanRead(olist[i]) && !object_access_override(&olist[i]) )
        return error_handler( INVALIDACCESS ) ;
      if ( theLen( olist[ i ]) != (crdInfo->renderTable16bit ?
                                   2 * crdInfo->dimension * nb * nc :
                                   crdInfo->dimension * nb * nc ))
        return error_handler( RANGECHECK ) ;
    }
    /* Wow: it's ok, keep a copy */
    Copy( & crdInfo->rendertable , theo ) ;
    crdInfo->renderTablePresent = TRUE ;
  }

  /* Check the values of RangeABC are in the range 0->1 for the case
   * where there is no valid render table
   */
  if ((thematch[crd_RenderTable].result == NULL) || (na == 0 && nb == 0 && nc == 0)) {
    if ((crdInfo->rangeabc[0] < 0.0) || (crdInfo->rangeabc[1] > 1.0) ||
        (crdInfo->rangeabc[2] < 0.0) || (crdInfo->rangeabc[3] > 1.0) ||
        (crdInfo->rangeabc[4] < 0.0) || (crdInfo->rangeabc[5] > 1.0))
      return error_handler(RANGECHECK);
  }

  /* Know we have a valid CRD by this stage, and have set up pointers
   * to C code if we are in optimised cases. If not, then we will need
   * to callout to PS. Do this now rather than later, and cache the results,
   * to avoid interpreter calls during rendering.
   */
  if ( !crd_cache_ps_calls(crdInfo) )
    return FALSE;

  return cc_create_xuids( thematch[crd_UniqueID].result ,
                          thematch[crd_XUID].result ,
                          & crdInfo->nXUIDs ,
                          & crdInfo->pXUIDs ) ;
}

static Bool cc_setcolorrendering_2( GS_COLORinfo *colorInfo, OBJECT *crdObject )
{
  enum {crd_Intent};
  static NAMETYPEMATCH thematch[] = {
    { NAME_Intent,                         1, { ONAME }},
    DUMMY_END_MATCH
  };

  OBJECT *theo ;

  if (!dictmatch(crdObject, thematch))
    return FALSE ;

  theo = thematch[crd_Intent].result ;
  if (!gsc_setrenderingintent(colorInfo, theo))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
OBJECT *gsc_getcolorrendering (GS_COLORinfo *colorInfo)
{
  GS_CRDinfo *crdInfo ;

  HQASSERT(colorInfo != NULL, "colorInfo NULL");

  /* Make sure there is a crdInfo associated with the current gstate */
  crdInfo = cc_getCrdInfo(colorInfo) ;
  if (crdInfo == NULL) {
    HQFAIL("No current CRD. This should not happen, even at startup");
    return &onull;
  }

  return &crdInfo->colorrendering;
}

/* ---------------------------------------------------------------------- */
Bool cc_crdpresent(GS_CRDinfo* crdInfo)
{
  if ( crdInfo == NULL)
    return FALSE;

  return (oType(crdInfo->colorrendering) != ONULL);
}

/* ---------------------------------------------------------------------- */
static Bool identityMatrix(SYSTEMVALUE matrix[9])
{
  return (matrix[0] == 1.0 && matrix[1] == 0.0 && matrix[2] == 0.0 &&
          matrix[3] == 0.0 && matrix[4] == 1.0 && matrix[5] == 0.0 &&
          matrix[6] == 0.0 && matrix[7] == 0.0 && matrix[8] == 1.0);
}

/* ---------------------------------------------------------------------- */
CIECallBack findEncodeabcProc(OBJECT *proc, ENCODEABC_DATA *encodeabcData)
{
  OBJECT *olist;
  CIECallBack returnFunc = NULL;

  HQASSERT(oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY, "Bad proc");
  olist = oArray(*proc);

  /* Optimise for ICC EncodeABC procs
   * - [/real sub /real div [numbers] interpop]
   */
  if (theLen(*proc) == 6 &&
      oType(olist[0]) == OREAL &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_sub &&
      oType(olist[2]) == OREAL &&
      oType(olist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[3]))) == NAME_div &&
      oType(olist[4]) == OARRAY &&
      oType(olist[5]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[5]))) == NAME_interpop) {
    encodeabcData->sub = oReal(olist[0]);
    encodeabcData->div = oReal(olist[2]);
    OCopy(encodeabcData->array, olist[4]);

    if (interpopCheck(&encodeabcData->array)) {
      encodeabcData->present = TRUE;
      returnFunc = encodeabc_ICC_1;
      return returnFunc;
    }
  }

  /* Optimise for A flavour of ICC EncodeABC procs.  Found in CRDs prior to
   * introducing ConvertXYZtoLab.
   * - [116 mul 16 sub /real sub /real div [numbers] interpop]
   */
  if (theLen(*proc) == 10 &&
      oType(olist[0]) == OINTEGER && oInteger(olist[0]) == 116 &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_mul &&
      oType(olist[2]) == OINTEGER && oInteger(olist[2]) == 16 &&
      oType(olist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[3]))) == NAME_sub &&
      oType(olist[4]) == OREAL &&
      oType(olist[5]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[5]))) == NAME_sub &&
      oType(olist[6]) == OREAL &&
      oType(olist[7]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[7]))) == NAME_div &&
      oType(olist[8]) == OARRAY &&
      oType(olist[9]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[9]))) == NAME_interpop) {
    encodeabcData->sub = oReal(olist[4]);
    encodeabcData->div = oReal(olist[6]);
    OCopy(encodeabcData->array, olist[8]);

    if (interpopCheck(&encodeabcData->array)) {
      encodeabcData->present = TRUE;
      returnFunc = encodeabc_ICC_2;
      return returnFunc;
    }
  }

  /* Optimise for Hqn EncodeABC procs
   * - [/int mul /int sub [numbers] abcop]
   */
  if (theLen(*proc) == 6 &&
      oType(olist[0]) == OINTEGER &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_mul &&
      oType(olist[2]) == OINTEGER &&
      oType(olist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[3]))) == NAME_sub &&
      oType(olist[4]) == OARRAY &&
      oType(olist[5]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[5]))) == NAME_abcop) {
    encodeabcData->mul = oInteger(olist[0]);
    encodeabcData->sub = oInteger(olist[2]);
    OCopy(encodeabcData->array, olist[4]);

    if (abcopCheck(&encodeabcData->array)) {
      encodeabcData->present = TRUE;
      returnFunc = encodeabc_Hqn_1;
      return returnFunc;
    }
  }

  /* Optimise for Hqn EncodeABC procs
   * - [[numbers] abcop]
   */
  if (theLen(*proc) == 2 &&
      oType(olist[0]) == OARRAY &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_abcop) {
    OCopy(encodeabcData->array, olist[0]);

    if (abcopCheck(&encodeabcData->array)) {
      encodeabcData->present = TRUE;
      returnFunc = encodeabc_Hqn_2;
      return returnFunc;
    }
  }

  return NULL;
}

static SYSTEMVALUE encodeabc_ICC_1(SYSTEMVALUE arg, void *extra)
{
  ENCODEABC_DATA *encodeabcData = (ENCODEABC_DATA *) extra;

  arg -= encodeabcData->sub;
  arg /= encodeabcData->div;

  return interpop(arg, &encodeabcData->array);
}

static SYSTEMVALUE encodeabc_ICC_2(SYSTEMVALUE arg, void *extra)
{
  ENCODEABC_DATA *encodeabcData = (ENCODEABC_DATA *) extra;

  arg *= 116.0;
  arg -= 16.0;
  arg -= encodeabcData->sub;
  arg /= encodeabcData->div;

  return interpop(arg, &encodeabcData->array);
}

static SYSTEMVALUE encodeabc_Hqn_1(SYSTEMVALUE arg, void *extra)
{
  ENCODEABC_DATA *encodeabcData = (ENCODEABC_DATA *) extra;

  arg *= encodeabcData->mul;
  arg -= encodeabcData->sub;

  return abcop(arg, &encodeabcData->array);
}

static SYSTEMVALUE encodeabc_Hqn_2(SYSTEMVALUE arg, void *extra)
{
  ENCODEABC_DATA *encodeabcData = (ENCODEABC_DATA *) extra;

  return abcop(arg, &encodeabcData->array);
}

CIECallBack findEncodelmnProc(OBJECT *proc, ENCODELMN_DATA *encodelmnData)
{
  OBJECT *olist;
  CIECallBack returnFunc = NULL;

  HQASSERT(oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY, "Bad proc");
  olist = oArray(*proc);

  /* Optimise for Hqn L & N flavours of EncodeLMN procs. Found in CRDs prior to
   * introducing ConvertXYZtoLab.
   * - {/real div dup /real le {/real mul 16 116 div add} {1 3 div exp} ifelse}
   */
  if (theLen(*proc) == 8 &&
      oType(olist[0]) == OREAL &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_div &&
      oType(olist[2]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[2]))) == NAME_dup &&
      oType(olist[3]) == OREAL &&
      oType(olist[4]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[4]))) == NAME_le &&
      oType(olist[5]) == OARRAY && lmnIfArrayCheck(&olist[5]) &&
      oType(olist[6]) == OARRAY && lmnElseArrayCheck(&olist[6]) &&
      oType(olist[7]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[7]))) == NAME_ifelse) {
    encodelmnData->div = oReal(olist[0]);
    encodelmnData->le = oReal(olist[3]);
    encodelmnData->mul = oReal(oArray(olist[5])[0]);

    encodelmnData->present = TRUE;
    returnFunc = encodelmn_LN;
    return returnFunc;
  }

  /* Optimise for Hqn M flavour of EncodeLMN procs. Found in CRDs prior to
   * introducing ConvertXYZtoLab.
   * - {dup 6 29 div dup dup mul mul le {/real mul 16 116 div add} {1 3 div exp} ifelse}
   */
  if (theLen(*proc) == 12 &&
      oType(olist[0]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[0]))) == NAME_dup &&
      oType(olist[1]) == OINTEGER && oInteger(olist[1]) == 6 &&
      oType(olist[2]) == OINTEGER && oInteger(olist[2]) == 29 &&
      oType(olist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[3]))) == NAME_div &&
      oType(olist[4]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[4]))) == NAME_dup &&
      oType(olist[5]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[5]))) == NAME_dup &&
      oType(olist[6]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[6]))) == NAME_mul &&
      oType(olist[7]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[7]))) == NAME_mul &&
      oType(olist[8]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[8]))) == NAME_le &&
      oType(olist[9]) == OARRAY && lmnIfArrayCheck(&olist[9]) &&
      oType(olist[10]) == OARRAY && lmnElseArrayCheck(&olist[10]) &&
      oType(olist[11]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[11]))) == NAME_ifelse) {
    encodelmnData->mul = oReal(oArray(olist[9])[0]);

    encodelmnData->present = TRUE;
    returnFunc = encodelmn_M;
    return returnFunc;
  }

  return NULL;
}

static Bool lmnIfArrayCheck(OBJECT *array)
{
  int32       alen = theLen(*array);
  OBJECT      *alist = oArray(*array);
  if (alen != 6)
    return FALSE;

  return oType(alist[0]) == OREAL &&
         oType(alist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(alist[1]))) == NAME_mul &&
         oType(alist[2]) == OINTEGER && oInteger(alist[2]) == 16 &&
         oType(alist[3]) == OINTEGER && oInteger(alist[3]) == 116 &&
         oType(alist[4]) == OOPERATOR && theINameNumber(theIOpName(oOp(alist[4]))) == NAME_div &&
         oType(alist[5]) == OOPERATOR && theINameNumber(theIOpName(oOp(alist[5]))) == NAME_add;
}

static Bool lmnElseArrayCheck(OBJECT *array)
{
  int32       alen = theLen(*array);
  OBJECT      *alist = oArray(*array);
  if (alen != 4)
    return FALSE;

  return oType(alist[0]) == OINTEGER && oInteger(alist[0]) == 1 &&
         oType(alist[1]) == OINTEGER && oInteger(alist[1]) == 3 &&
         oType(alist[2]) == OOPERATOR && theINameNumber(theIOpName(oOp(alist[2]))) == NAME_div &&
         oType(alist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(alist[3]))) == NAME_exp;
}

static SYSTEMVALUE encodelmn_LN(SYSTEMVALUE arg, void *extra)
{
  ENCODELMN_DATA *encodelmnData = (ENCODELMN_DATA *) extra;

  arg /= encodelmnData->div;
  if (arg <= encodelmnData->le) {
    arg *= encodelmnData->mul;
    arg += 16.0/116.0;
  }
  else {
    arg = pow(arg, 1.0/3.0);
  }

  return arg;
}

static SYSTEMVALUE encodelmn_M(SYSTEMVALUE arg, void *extra)
{
  ENCODELMN_DATA *encodelmnData = (ENCODELMN_DATA *) extra;

  if (arg <= (6.0/29.0) * (6.0/29.0) * (6.0/29.0)) {
    arg *= encodelmnData->mul;
    arg += 16.0/116.0;
  }
  else {
    arg = pow(arg, 1.0/3.0);
  }

  return arg;
}

CIECallBack findRenderTableProc(OBJECT *proc, RENDERTABLE_DATA *renderTableData)
{
  OBJECT *olist;
  CIECallBack returnFunc = NULL;

  HQASSERT(oType(*proc) == OARRAY || oType(*proc) == OPACKEDARRAY, "Bad proc");
  olist = oArray(*proc);

  /* Optimise for ICC release build RenderTable procs
   * - [[numbers] interpop]
   */
  if (theLen(*proc) == 2 &&
      oType(olist[0]) == OARRAY &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_interpop) {

    OCopy(renderTableData->array, olist[0]);

    if (interpopCheck(&renderTableData->array)) {
      renderTableData->present = TRUE;
      returnFunc = rendertable_1;
      return returnFunc;
    }
  }

  /* Optimise for ICC debug build RenderTable procs
   * - [[numbers] /real div interpop]
   */
  if (theLen(*proc) == 4 &&
      oType(olist[0]) == OARRAY &&
      oType(olist[1]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[1]))) == NAME_interpop &&
      oType(olist[2]) == OREAL &&
      oType(olist[3]) == OOPERATOR && theINameNumber(theIOpName(oOp(olist[3]))) == NAME_div) {

    OCopy(renderTableData->array, olist[0]);
    renderTableData->div = oReal(olist[2]);

    if (interpopCheck(&renderTableData->array)) {
      renderTableData->present = TRUE;
      returnFunc = rendertable_2;
      return returnFunc;
    }
  }

  return NULL;
}

static SYSTEMVALUE rendertable_1(SYSTEMVALUE val, void *extra)
{
  RENDERTABLE_DATA *renderTableData = (RENDERTABLE_DATA *) extra;

  return interpop(val, &renderTableData->array);
}

static SYSTEMVALUE rendertable_2(SYSTEMVALUE val, void *extra)
{
  RENDERTABLE_DATA *renderTableData = (RENDERTABLE_DATA *) extra;

  val = rendertable_1(val, extra);
  val /= renderTableData->div;

  return val;
}

static Bool interpopCheck(OBJECT *array)
{
  int32       i;
  int32       alen = theLen(*array);
  OBJECT      *alist = oArray(*array);
  if (alen < 2)
    return FALSE;
  for (i = 0; i < alen; i++) {
    if (oType(alist[i]) != OREAL && oType(alist[i]) != OINTEGER)
      return FALSE;
  }

  return TRUE;
}

static SYSTEMVALUE interpop(SYSTEMVALUE val, OBJECT *array)
{
  int32       alen = theLen(*array);
  OBJECT      *alist = oArray(*array);
  int32       lowidx;
  SYSTEMVALUE lowval;
  SYSTEMVALUE highval;

  /* Check that we have 2 or more entries in the array */
  HQASSERT(alen >= 2, "alen < 2");

  /* We expect an input value between 0 and 1. Clip if necessary.
   */
  NARROW_01(val);

  /* Get the index of the low value */
  lowidx = (int32) ((alen - 1) * val);

  /* Get the low value of the interpolator */
  alist += lowidx;
  if ( !object_get_numeric(alist++, &lowval) )
    HQFAIL("Object should be prior tested to be numeric");

  /* If at the top of the array, consume the interpolation array
   * and return the extreme array value */
  if ( lowidx == alen - 1 )
    return lowval;

  /* Get the high value of the interpolator */
  if ( !object_get_numeric(alist, &highval) )
    HQFAIL("Object should be prior tested to be numeric");

  /* Found the interval - do interpolation */
  return (val * (alen - 1) - lowidx) * (highval - lowval) + lowval;
}

static Bool abcopCheck(OBJECT *array)
{
  int32       alen = theLen(*array);
  OBJECT*     alist = oArray(*array);
  SYSTEMVALUE lowcol;
  SYSTEMVALUE highcol;
  SYSTEMVALUE idx;
  if (alen < 4)
    return FALSE;
  if ( !object_get_numeric(alist++, &lowcol) ||
       !object_get_numeric(alist++, &idx) )
    return FALSE;
  while ( (alen -= 2) > 0 ) {
    if ( !object_get_numeric(alist++, &highcol) ||
         !object_get_numeric(alist++, &idx) )
      return FALSE;
    /* Ensure colour values monotonic increasing */
    if ( highcol < lowcol ) {
      return FALSE;
    }
    lowcol = highcol;
  }

  return TRUE;
}

static SYSTEMVALUE abcop(SYSTEMVALUE val, OBJECT *array)
{
  int32       alen = theLen(*array);
  OBJECT*     alist = oArray(*array);
  SYSTEMVALUE lowcol;
  SYSTEMVALUE highcol;
  SYSTEMVALUE lowidx = 0;
  SYSTEMVALUE highidx;

  /* Check that we have 2 or more entries in the array */
  HQASSERT(alen >= 4, "alen < 4");

  /* Get the initial low end of the interpolator */
  if ( !object_get_numeric(alist++, &lowcol) ||
       !object_get_numeric(alist++, &lowidx) )
    HQFAIL("Object should be prior tested to be numeric");

  if ( val < lowcol ) {
    /* Clip to lower index */
    return lowidx;
  }

  /* Loop over pairs to find interval to interpolate in */
  while ( (alen -= 2) > 0 ) {
    /* Find interval upper colour value */
    if ( !object_get_numeric(alist++, &highcol) )
      HQFAIL("Object should be prior tested to be numeric");

    HQASSERT(highcol >= lowcol, "Object should be prior tested to be monotonic");

    /* Find interval upper index value */
    if ( !object_get_numeric(alist++, &highidx) )
      HQFAIL("Object should be prior tested to be numeric");

    if ( (val >= lowcol) && (val <= highcol) ) {
      /* Found the interval - do interpolation */
      return ((val - lowcol) / (highcol - lowcol)) * (highidx - lowidx) + lowidx;

    } else { /* Use range upper values as next range lower values */
      lowcol = highcol;
      lowidx = highidx;
    }
  }

  /* Consume the interpolation array and return last index value */
  return lowidx;
}

/* end of gsccrd.c */


/* Log stripped */
