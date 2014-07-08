/** \file
 * \ingroup images
 *
 * $HopeName: SWv20!export:imexpand.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image data expander routines
 */

#ifndef __IMEXPAND_H__
#define __IMEXPAND_H__

#include "dl_color.h"
#include "objnamer.h"

struct render_blit_t; /* from render.h */
struct render_info_t; /* from render.h */
struct blit_color_t ; /* from CORErender */
struct DL_STATE ;
struct IM_STORE;

/* The following enums define the format of the data the expand routines expect
 * to be able to read from the image store. If the data is not in this format,
 * then it must be put into the image store in that format.
 */
enum {
  ime_planar = 0,
  ime_colorconverted = 1,
  /* interleaved values must not change; they are used to specify bpp
   * for the image store. */
  ime_interleaved2 = 2,
  ime_interleaved4 = 4,
  ime_interleaved8 = 8,
  ime_interleaved16 = 16,
  ime_as_is_decoded = 41,  /**< Arbitrary value, image samples are decoded
                                using decode arrays, but otherwise unaltered */
  ime_as_is = 42           /**< Arbitrary value, image samples are unaltered */
} ;

#define IME_ALLPLANES (-1)

/** This enum is used by im_expand{lut,data}test to return information about
    the image data. */
enum {
  imcolor_error,
  imcolor_same,
  imcolor_different,
  imcolor_unknown
} ;

typedef struct IM_EXPAND IM_EXPAND ;
typedef struct IM_EXPBUF IM_EXPBUF ;

IM_EXPBUF **im_createexpbuf(struct DL_STATE *page, Bool multi_thread);
void im_freeexpbuf(struct DL_STATE *page, IM_EXPBUF **) ;

void im_expand_colorinfo_free(DL_STATE *page, Bool otf_possible);
void im_expanderase(DL_STATE *page);

int32 im_expandformat(const IM_EXPAND *ime) ;
Bool im_expandpad(const IM_EXPAND *ime) ;
int32 im_expandbpp(const IM_EXPAND *ime) ;
int32 im_expandobpp(const IM_EXPAND *ime) ;
Bool im_expand1bit(const struct blit_color_t *color, const IM_EXPAND *ime,
                   const int expanded_to_plane[], unsigned int expanded_comps,
                   const int blit_to_expanded[]) ;
int32 im_expandluttest(const IM_EXPAND *ime, COLORANTINDEX ci, COLORVALUE ecv) ;
int32 im_expanddatatest( IM_EXPAND *ime, struct IM_STORE *ims,
                         COLORANTINDEX ci, COLORVALUE ecv,
                         const ibbox_t *ibbox) ;
Bool im_expandmasktest(DL_STATE *page, const IM_EXPAND *ime, Bool *polarity) ;

IM_EXPAND *im_expandopenimask(DL_STATE *page, int32 width, int32 height,
                              int32 ibpp, int32 polarity) ;
IM_EXPAND *im_expandopenimage(DL_STATE *page, float *fdecodes[], int32 *ndecodes[] ,
                              float *decode_array, void *decode_for_adjust,
                              int32 decodes_incomps, Bool independent,
                              int32 width , int32 height ,
                              int32 incomps, int32 ibpp, Bool fixedpt16,
                              int32 oncomps , Bool out16 ,
                              Bool interleaved , int32 plane ,
                              GS_COLORinfo *colorInfo, int32 colorType ,
                              int32 imagetype , int32 method ,
                              Bool isSoftMask ,
                              Bool allowLuts, Bool allowInterleavedLuts,
                              uint8 defaultFormat);

void im_expand_select(/*@notnull@*/ IM_EXPAND *ime,
                      SPOTNO spotFilter, HTTYPE typeFilter);


typedef void *(IM_EXPANDFUNC)(
       /*@notnull@*/ /*@in@*/ IM_EXPAND*    ime,
                     /*@in@*/ struct IM_STORE*     ims ,
                             /* Ooh, yuck, a multi-purpose parameter! This is
                                a single parameter because it makes it much
                                easier to unify the expander prototypes; in
                                1:1 optimisations, it is xflip, whether the
                                expanded should reverse the data while
                                expanding. In non 1:1 cases, it is a boolean
                                to indicate if the renderer is being used for
                                compositing. */
                              Bool adjust,
                              int32         x,
                              int32         y,
                              int32         n,
                              int32         *nrows,
       /*@notnull@*/ /*@in@*/ const int expanded_to_plane[],
                              unsigned int nexpanded);
IM_EXPANDFUNC im_expandread;
IM_EXPANDFUNC im_expand1to1;
IM_EXPANDFUNC im_expandknockout;

Bool im_expand_setup_on_the_fly(const IMAGEOBJECT *imageobj, DL_STATE* page,
                                int32 method, GS_COLORinfo *colorInfo,
                                int32 colorType);

void im_expand_attach_alternate(IM_EXPAND *ime, IM_EXPAND *ime_alternate);

/** Indicates whether the image is being color-converted on the fly as
    it is expanded (when direct-rendering). */
Bool im_converting_on_the_fly(const IM_EXPAND *ime);

/** Are we getting 16 bpc from the image? */
Bool im_16bit_output(const struct render_info_t* p_ri, const IM_EXPAND *ime);


void im_expandfree(DL_STATE *page, IM_EXPAND *ime) ;
Bool im_expandmerge(DL_STATE *page, IM_EXPAND *ime_src, IM_EXPAND *ime_dst) ;

void *im_expand_decode_array(const IM_EXPAND *ime);
Bool im_expandlutexists(IM_EXPAND *ime, int32 plane) ;
int32 im_expandiplanes( IM_EXPAND *ime ) ;
void  im_expandpresepread( IM_EXPAND *ime ,
                           Bool fSubtractive ,
                           float *buf ,
                           int32 plane ) ;
int32 im_expandadjustplane(IM_EXPAND* ime, int32 i);

Bool im_expanduniform(IM_EXPAND *ime, struct IM_STORE *ims,
                      COLORVALUE *buf, int32 buflen);

Bool im_expandtolut(struct DL_STATE *page, IM_EXPAND *ime,
                    GS_COLORinfo *colorInfo, COLORANTINDEX ci) ;

void im_expandplanefree(DL_STATE *page, IM_EXPAND* ime, int32 plane);
Bool im_expandreorder(DL_STATE *page, IM_EXPAND* ime,
                      const COLORANTINDEX order[], int32 nplanes);
Bool im_expandrecombined(IM_EXPAND *ime, DL_STATE *page,
                         const COLORANTINDEX rcbmap[], int32 maplength);

Bool im_expandalphachannel(IM_EXPAND* ime,
                           struct IM_STORE* ims_alpha);
struct IM_STORE* im_expand_ims_alpha(IM_EXPAND* ime);
struct IM_STORE* im_expand_detach_alphachannel(
                 /*@notnull@*/ /*@in@*/ IM_EXPAND *ime) ;

const /*@dependent@*/ COLORANTINDEX *im_getcis(
                        /*@notnull@*/ /*@in@*/ const IM_EXPAND *ime);

/** Get the plane index for a colorant index. */
int32 im_ci2plane(/*@notnull@*/ /*@in@*/ const IM_EXPAND *ime,
                  COLORANTINDEX ci);

/* These routines perform the mapping to and from RLE colour values. They
   are a little out of place here, but need exporting for HDLT, and this is
   the only existing place that seems related. */
void rle_map_colors(int32 valuesPerComponent, uint16 *lut , int32 count);
void rle_unmap_colors(int32 valuesPerComponent, uint16 *inlut ,
                      uint16 *outlut, int32 count);

/** Prepare template color for image expansion, and create mappings. We
    create two mappings, one from the expanded data order to the expander's
    plane index for use within the expander, and another from the expanded
    data order to the blit color channels. Mapping this way lets us keep the
    expanded data compact, makes filling the blit color fast, whilst
    permitting mapping multiple channels to the All plane. (If there are
    multiple mappings of the All plane, they will be reflected in the
    expanded data.)  Because on-the-fly conversion can change the set of
    channels after expansion, this returns lengths before and after. */
void im_expand_blit_mapping(/*@notnull@*/ const struct render_blit_t *rb,
                            /*@in@*/ /*@notnull@*/ IM_EXPAND *ime,
                            /*@out@*/ /*@notnull@*/ int expanded_to_plane[],
                            /*@out@*/ /*@notnull@*/ unsigned int *n_expanded,
                            /*@out@*/ /*@notnull@*/ unsigned int *n_converted,
                            /*@out@*/ /*@notnull@*/ int blit_to_expanded[]);

/** Creates a blit mapping from the expanded data order to the
    expander's plane index for use within the expander, and returns the
    sizes of image data produced from the on-the-fly colour converter
    within the expander.  This is the data needed for the separation
    omission code to examine the image colours in the otf case. */
void im_extract_blit_mapping(/*@in@*/ /*@notnull@*/ IM_EXPAND *ime,
                             /*@out@*/ /*@notnull@*/ int expanded_to_plane[],
                             /*@out@*/ /*@notnull@*/ unsigned int *n_expanded,
                             /*@out@*/ /*@notnull@*/ unsigned int *n_converted,
                             /*@out@*/ /*@notnull@*/ size_t *converted_bits,
                             /*@out@*/ /*@notnull@*/ COLORANTINDEX **output_colorants);

struct core_init_fns; /* from SWcore */
void im_expand_C_globals(struct core_init_fns *fns);

#endif /* protection for multiple inclusion */


/* Log stripped */
