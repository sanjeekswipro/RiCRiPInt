/** \file
 * \ingroup color
 *
 * $HopeName: COREgstate!color:src:icmini.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Modular colour chain processor for handling ICC profiles.
 */

#ifndef __ICMINI_H__
#define __ICMINI_H__

#include "gs_color.h"     /* COLORSPACE_ID */
#include "gs_colorpriv.h" /* CLINKiccbased */
#include "fileio.h"
#include "md5.h"
#include "gsctoicc.h"

/*----------------------------------------------------------------------------*/
/* The struct hack, immortalised in macros
 *
 * For each STRUCTURE using the hack, there is a STRUCTURE_SIZE(n) macro that
 * calculates the required size (unrounded) for a struct with n entries. Note
 * that it is therefore NOT necessary to pad a hack byte array to a word
 * boundary - size calculations WILL be correct anyway.
 */

#define HACK 1     /* Only C99 likes [] as the struct hack, so [1] it must be */

/*----------------------------------------------------------------------------*/
/* undefine this for float intermediate values (USERVALUE actually)
 * define this for doubles
 */

#define IC_USE_DOUBLES

#ifdef IC_USE_DOUBLES
typedef double ICVALUE;
#else
typedef USERVALUE ICVALUE;
#endif

#define MAX_CHANNELS 15

#define MAX_SCRATCH_SIZE 65384

#define L_RANGE     100.0f    /* range of L values for Lab devicespace */
#define L_MIN_VALUE 0.0f      /* minimum L value for Lab devicespace */
#define L_MAX_VALUE 100.0f    /* maximum L value for Lab devicespace */

#define AB_RANGE     255.0f   /* range of a or b value for Lab devicespace */
#define AB_MIN_VALUE -128.0f  /* minimum a or b value for Lab devicespace */
#define AB_MAX_VALUE 127.0f   /* maximum a or b value for Lab devicespace */

#define INVALID_PTR ( (void*) ((intptr_t) mi_alloc) )

/*----------------------------------------------------------------------------*/

/* allocs don't remember how big they are, which is unhelpful */

typedef struct MI_ALLOC {
  size_t size;
  uint8 data[HACK];
} MI_ALLOC;

#define MI_ALLOC_SIZE(_n) \
  (offsetof(MI_ALLOC,data[0])+(_n)*sizeof(uint8))

void* mi_alloc(size_t size);
void  mi_free(void* data);

/*----------------------------------------------------------------------------*/
/* The ICCBased invoke is a general-purpose modular call.
 * Its private data is a list of function and data pointers, which
 * are called in turn and act upon the output colorant buffer, into
 * which the invoke will have copied and ranged the input colorants.
 *
 * The data for each mini-invoke is as follows
 */

/* 16bit clut mini-invoke data */

typedef struct MI_CLUT16 {
  uint32  step[16];        /* step size for each dimension */
  uint8   in;              /* number of input channels */
  uint8   out;             /* number of output channels */
  uint8   maxindex[16];    /* gridpoints minus one per dimension */
  uint8   spare;
  ICVALUE *scratch;
  uint16  values[HACK];    /* values from profile, endian-fixed */
} MI_CLUT16;

#define MI_CLUT16_SIZE(_n) \
  (offsetof(MI_CLUT16,values[0])+(_n)*sizeof(uint16))

/* data referred to must be of form MI_CLUT16 */
void mi_clut16(void* data, ICVALUE* colorant);

/* 8bit clut mini-invoke data  */

typedef struct MI_CLUT8 {
  uint32  step[16];        /* step size for each dimension */
  uint8   in;              /* number of input channels */
  uint8   out;             /* number of output channels */
  uint8   maxindex[16];    /* gridpoints minus one per dimension */
  ICVALUE *scratch;
  uint8   values[HACK];    /* values from profile */
} MI_CLUT8;

#define MI_CLUT8_SIZE(_n) \
  (offsetof(MI_CLUT8,values[0])+(_n)*sizeof(uint8))

/* data referred to must be of form MI_CLUT8 */
void mi_clut8(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* Piecewise linear mini-invoke */

typedef struct DATA_PIECEWISE_LINEAR {
  struct DATA_PIECEWISE_LINEAR *next;
  uint32     maxindex;     /* number of values minus one */
  USERVALUE  values[HACK]; /* values from profile, prescaled */
} DATA_PIECEWISE_LINEAR;

#define DATA_PIECEWISE_LINEAR_SIZE(_n) \
  (offsetof(DATA_PIECEWISE_LINEAR, values[0]) + (_n) * sizeof(USERVALUE))

typedef struct MI_PIECEWISE_LINEAR {
  uint32                 channelmask;
  DATA_PIECEWISE_LINEAR  curves[HACK];
} MI_PIECEWISE_LINEAR;

/* we cannot calculate the length of an MI_PIECEWISE_LINEAR given only the
 * number of curves, since DATA_PIECEWISE_LINEARs are themselves variable
 * length. So, we will define the length of the header only. */
#define MI_PIECEWISE_LINEAR_SIZE \
  (offsetof(MI_PIECEWISE_LINEAR,curves[0]))

/* data referred to must be of form MI_PIECEWISE_LINEAR */
void mi_piecewise_linear(void* data, ICVALUE* colorant);
void mi_inverse_linear(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* parametric mini-invoke */

typedef struct DATA_PARAMETRIC {
  USERVALUE  gamma;
  USERVALUE  a;
  USERVALUE  b;
  USERVALUE  c;
  USERVALUE  d;
  USERVALUE  e;
  USERVALUE  f;
} DATA_PARAMETRIC;

typedef struct MI_PARAMETRIC {
  uint32           channelmask;
  DATA_PARAMETRIC  curves[HACK];
} MI_PARAMETRIC;

#define MI_PARAMETRIC_SIZE(_n) \
  (offsetof(MI_PARAMETRIC,curves[0])+(_n)*sizeof(DATA_PARAMETRIC))

/* data referred to must be of form MI_PARAMETRIC */
void mi_parametric(void* data, ICVALUE* colorant);

/* inverse parametric mini-invoke */

typedef struct DATA_INVERSE_PARAMETRIC {
  DATA_PARAMETRIC p;    /* the parameters (a, c and gamma inverted) */
  USERVALUE  crv_min;   /* domain of the gamma function Y=(aX+b)^g+e */
  USERVALUE  crv_max;
  USERVALUE  lin_min;   /* domain of the linear function Y=cX+f */
  USERVALUE  lin_max;
  USERVALUE  gap_min;   /* domain of the gap between the above functions */
  USERVALUE  gap_max;
  USERVALUE  minimum;   /* minimum of the above three domains */
  USERVALUE  above;     /* X to use for above (implicit) maximum Y */
  USERVALUE  below;     /* X to use for below (stated) minimum Y */
  USERVALUE  gradient;
  USERVALUE  offset;    /* equation of gap interpolation */
} DATA_INVERSE_PARAMETRIC;

typedef struct MI_INVERSE_PARAMETRIC {
  uint32           channelmask;
  DATA_INVERSE_PARAMETRIC  curves[HACK];
} MI_INVERSE_PARAMETRIC;

#define MI_INVERSE_PARAMETRIC_SIZE(_n) \
  (offsetof(MI_INVERSE_PARAMETRIC,curves[0])+(_n)*sizeof(DATA_INVERSE_PARAMETRIC))

/* data referred to must be of form MI_INVERSE_PARAMETRIC */
void mi_inverse_parametric(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* flip mini-invoke */

typedef struct MI_FLIP {
  uint32  channels;
} MI_FLIP;

/* data referred to must be of form MI_FLIP */
void mi_flip(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* matrix mini-invoke */

typedef struct MI_MATRIX {
  SYSTEMVALUE matrix[3][4];
  Bool clip;   /* Whether to clip to the range 0.0f to 1.0f - don't for TRCs */
} MI_MATRIX;

/* data referred to must be of form MI_MATRIX */
void mi_matrix(void* data, ICVALUE* colorant);
void mi_scale(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* XYZ conversion */

typedef struct MI_XYZ {
  ICVALUE   scale[3];
  XYZVALUE  relative_whitepoint;
} MI_XYZ;

typedef struct MI_MULTIPLY {
  ICVALUE  color[3];
} MI_MULTIPLY;

/* data referred to must be of form MI_XYZ */
void mi_lab2xyz(void* data, ICVALUE* colorant);
void mi_xyz2lab(void* data, ICVALUE* colorant);

/* data referred to must be of form MI_XYZ */
void mi_xyz2xyz(void* data, ICVALUE* colorant);

/* data referred to must be of form MI_MULTIPLY */
void mi_multiply(void* data, ICVALUE* colorant);

void mi_neutral_ab(void* data, ICVALUE* colorant);

/*----------------------------------------------------------------------------*/
/* Colorant names for NCLR profiles */
/* Note we don't store color values */

typedef struct COLORANT_DATA {
  int8        n_colors;
  int8        spare1;
  int8        spare2;
  int8        spare3;
  NAMECACHE * colorantname[HACK];
} COLORANT_DATA;

#define COLORANT_DATA_SIZE(_n) \
  (offsetof(COLORANT_DATA,colorantname[0])+(_n)*sizeof(NAMECACHE*))

#define COLORANTS_ABSENT ((COLORANT_DATA*) INVALID_PTR )

/*----------------------------------------------------------------------------*/
/* action list */

typedef void (*MINI_INVOKE)(void* data, ICVALUE* colorant);

typedef struct DATA_ACTION {
  MINI_INVOKE              function;
  union {
    void*                  data;
    MI_PIECEWISE_LINEAR*   piecewise_linear;
    MI_PARAMETRIC*         parametric;
    MI_INVERSE_PARAMETRIC* inverse_parametric;
    MI_FLIP*               flip;
    MI_MULTIPLY*           multiply;
    MI_MATRIX*             matrix;
    MI_CLUT16*             clut16;
    MI_CLUT8*              clut8;
    MI_XYZ*                xyz2xyz;
    MI_XYZ*                lab2xyz;
    uint32                 remaining;  /* last (function==0) entry only! */
  } u;
} DATA_ACTION;

typedef struct ICC_PROFILE_ID {
  int32                 xref;          /* XPS partname uid or PDF object num/generation composite */
  int32                 contextID;     /* PDF execution context ID */
} ICC_PROFILE_ID;

typedef struct ICC_FILELIST_INFO {
  struct ICC_FILELIST_INFO *next;
  FILELIST*               file;            /* the profile data stream (or 0) */
  int32                   sid;             /* save level (or -1 if closed) of 'file' */
  FILELIST*               orig_file;       /* original (unRSD'd) stream or 0 */
  int32                   orig_sid;        /* save level (or -1 if closed) of 'orig_file' */
  ICC_PROFILE_ID          uniqueID;        /* so we can reuse a closed file */
} ICC_FILELIST_INFO;

/*----------------------------------------------------------------------------*/
/* profile cache */

#define N_ICC_TABLES    (3)

struct ICC_PROFILE_INFO_CACHE {
  ICC_PROFILE_INFO_CACHE *next;
  ICC_PROFILE_INFO       *d;
};

struct ICC_PROFILE_INFO {
  cc_counter_t            refCnt;
  ICC_FILELIST_INFO*      filelist_head;   /* linked list of ICC_FILELIST_INFOs */
  uint8                   md5[MD5_OUTPUT_LEN];        /* profileID */
  uint8                   header_md5[MD5_OUTPUT_LEN];
  Bool                    validMD5;
  Bool                    validHeaderMD5;
  Bool                    validProfile;    /* invalid profiles may not force an error when overriding */
  Bool                    abortOnBadICCProfile;
  Bool                    useAlternateSpace;
  Bool                    inputTablePresent;
  Bool                    outputTablePresent;
  Bool                    devicelinkTablePresent;
  Bool                    is_scRGB;        /* our special matrix only scRGB profile */

  icProfileClassSignature deviceClass;     /* from the profile header */
  int8                    n_device_colors; /* the number of device components */
  COLORSPACE_ID           devicespace;     /* the input colourspace */
  int8                    n_pcs_colors;    /* the number of pcs components */
  COLORSPACE_ID           pcsspace;        /* the pcs colorspace */

  uint8                   preferredIntent; /* from the profile header */

  XYZVALUE                whitepoint;
  XYZVALUE                blackpoint;
  XYZVALUE                relative_whitepoint;
  XYZVALUE                relative_blackpoint;

  CLINKiccbased*          dev2pcs[N_ICC_TABLES];  /* input links for the 3 intents */
  CLINKiccbased*          pcs2dev[N_ICC_TABLES];  /* output links for the 3 intents */

  COLORANT_DATA*          device_colorants; /* for NCLR profiles */
  COLORANT_DATA*          pcs_colorants;    /* for devlinks with NCLRs at PCS */

  int32                   devNsid;          /* save level for DeviceN objects (or -1) */

  OBJECT                  dev_DeviceNobj;   /* DeviceN space for NCLR profiles */
  OBJECT                  pcs_DeviceNobj;   /* DeviceN space for devlinks with NCLRs at PCS */

  int32                   profile_size;     /* the file size as claimed in profile header */
};

struct CLINKiccbased {
  cc_counter_t   refCnt;

  ICC_PROFILE_INFO *profile;
  uint8          intent;

  int8           i_dimensions;
  COLORSPACE_ID  iColorSpace;
  int8           o_dimensions;
  COLORSPACE_ID  oColorSpace;

  DATA_ACTION    actions[HACK];  /* the list of mini-invokes */
};

#define CLINKiccbased_SIZE(_n) \
  (offsetof(CLINKiccbased,actions[0])+(_n)*sizeof(DATA_ACTION))

#define DEFAULT_ACTION_LIST_LENGTH 8   /* initial action list length */
#define EXTEND_ACTION_LIST_LENGTH 8    /* amount to extend by once full */

Bool mi_add_mini_invoke(CLINKiccbased** pAction,
                        MINI_INVOKE function,
                        void*       data);

Bool mi_insert_mini_invoke(CLINKiccbased** pAction,
                           MINI_INVOKE function,
                           void*       data,
                           int32       before);

Bool iccbased_invokeActions( CLINKiccbased* data,
                             int32 first_action,
                             int32 n_actions,
                             int8 i_dimensions,
                             int8 o_dimensions,
                             USERVALUE* input,
                             USERVALUE* oColorValues );

Bool iccbased_invokeSingle(CLINK *pLink, USERVALUE *oColorValues);

/*----------------------------------------------------------------------------*/

#ifndef ASSERT_BUILD
#define iccbasedInfoAssertions(_pIccBasedInfo) EMPTY_STATEMENT()
#else
extern void iccbasedInfoAssertions(CLINKiccbased *pIccBasedInfo);
#endif

#endif

/* Log stripped */
