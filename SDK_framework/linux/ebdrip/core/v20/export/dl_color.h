/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_color.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This is the API for manipulating ncolor data.
 * Structures are revealed to allow some inlining of simple functions in
 * release build (and some additional checking in functions in a debug build).
 * If anyone uses the structure fields outside of these two files, I shall
 * slap their legs!
 */

#ifndef __DL_COLOR_H__
#define __DL_COLOR_H__

#include "coretypes.h"  /* Remove once core.h becomes prevalent. */
#include "graphict.h"   /* GUCR_RASTERSTYLE */
#include "displayt.h"   /* LISTOBJECT, DL_STATE */
#include "hqmemset.h"   /* LISTOBJECT, DL_STATE */
#include "gschead.h"    /* GSC_BLACK_TYPE */

typedef struct dlc_context_t dlc_context_t;

/** Pointer type for dl colors in dl objects. This is an opaque pointer:
 * the forward reference to a non-existent struct type is deliberate.
 */
typedef struct p_ncolor_t_opaque*   p_ncolor_t;


/** Define what a paint mask is
 */
typedef uint8   paint_mask_t;


/** Define dl color ref count variable type.  Ref counting include
 * copying data to new color when the ref count maxes out.
 * DLC_MAX_REFCOUNT holds what the max ref count is for the
 * underlying representation of a ref count.
 */
typedef uint16   ref_count_t;

#define DLC_MAX_REFCOUNT  ((ref_count_t)((1u << (8*sizeof(ref_count_t))) - 1))


/** Wrapper structure for accessing parts of a display list color
 * entry.  Pointers should be in increasing memory order i.e. prc <
 * ppm < pcv except for some special cases when pcv == NULL
 */
/* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
typedef struct color_entry_t {
  /*@null@*/ /*@dependent@*/
  ref_count_t*   prc;  /**< Pointer to ref count of dl color entry */
  /*@notnull@*/ /*@dependent@*/
  paint_mask_t*  ppm;  /**< Pointer to start of paint mask */
  /*@null@*/ /*@dependent@*/
  COLORVALUE*    pcv;  /**< Pointer to start of color channel values */
  int16          ccv;  /**< Number of color values in dl color */
  int16          iop;  /**< The index of overprint info in
                          the mask (-1 if none). Refers to
                          the first byte of the overprint
                          mask, _not_ the overprint escape
                          command */
  COLORVALUE     allsep;  /**< COLORVALUE_TRANSPARENT if not present. */
  COLORVALUE     opacity; /**< COLORVALUE_ONE means opaque. */
} color_entry_t;


/** Wrapper structure to hold meta info on color entry. NEVER declare these
 * "const".
 */
/* NOTE: When changing, update SWcore!testsrc:swaddin:swaddin.cpp. */
typedef struct dl_color_t {
  color_entry_t   ce;             /* pointers to color information */
  COLORANTINDEX   ci;             /* Last colorant accessed index */
  COLORVALUE      cv;             /* Value of last colorant accessed */
} dl_color_t;

/*
 * A number of functions are in-lined in a release build.  In a debug
 * build they are full function calls allowing asserts to be used,
 * especially for functions with return values

 * dlc_colorant_index()       - get current active colorant index for dl color
 * dlc_get_colorant()         - get color value for current active colorant index for dl color
 */

#define DLC_GET_INDEX(pdlc)             ((pdlc)->ci)
#define DLC_GET_COLORANT(pdlc)          ((pdlc)->cv)
#define DLC_NUM_CHANNELS(pdlc)          ((pdlc)->ce.ccv)
#define DLC_GET_CHANNEL(pdlc, offset)   ((pdlc)->ce.pcv[(offset)])


#if defined( ASSERT_BUILD )

COLORANTINDEX dlc_colorant_index_(
  const dl_color_t*     pdlc);      /* I */

COLORVALUE dlc_get_colorant_(
  const dl_color_t*     pdlc);      /* I */

/*
 * Macros to redirect calls to asserted functions
 */
#define dlc_colorant_index(pdlc)    dlc_colorant_index_(pdlc)
#define dlc_get_colorant(pdlc)      dlc_get_colorant_(pdlc)

#else /* ! defined( ASSERT_BUILD ) */

/*
 * Release build inlined function calls
 */
#define dlc_colorant_index(pdlc)    DLC_GET_INDEX(pdlc)
#define dlc_get_colorant(pdlc)      DLC_GET_COLORANT(pdlc)

#endif /* defined( ASSERT_BUILD ) */

#if defined( DEBUG_BUILD )

#define DLC_RESET(p) MACRO_START \
  HqMemSet8((uint8 *)(p), 0x55, sizeof(dl_color_t)); \
MACRO_END

#else /* ! defined( DEBUG_BUILD ) */

#define DLC_RESET(p)                EMPTY_STATEMENT();

#endif /* defined( DEBUG_BUILD ) */

/** Macro to clear out a dl wrapper - use to init new vars
 */
#define dlc_clear(pdlc) MACRO_START \
  DLC_RESET(pdlc);\
  (pdlc)->ce.prc = NULL; \
MACRO_END

/** Macro to test is dlc color is empty or not
 */
#define dlc_is_clear(pdlc)         ((pdlc)->ce.prc == NULL)

/** Create a dlc context and initialise the color cache. */
Bool dlc_context_create(DL_STATE *page);

/** Removes all unreferenced entries from the color cache after a partial
    paint. */
void dcc_purge(DL_STATE *page);

/** \brief Create a new dl color from the colorant indexes and values given.
 *
 * \see dlc_alloc_cmd()
 * \see dlc_alloc_interpolate()
 * \see dlc_alloc_fillin_template()
 */
Bool dlc_alloc_fillin(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         cci,          /* I */
  const COLORANTINDEX rgci[],       /* I */
  const COLORVALUE    rgcv[],       /* I */
  dl_color_t*   pdlc);       /* O */

/** \brief Create a new dl color from a number of existing dl colors and
 * weightings (used by shading).
 *
 * \see dlc_alloc_cmd()
 * \see dlc_alloc_fillin()
 * \see dlc_alloc_fillin_template()
 */

Bool dlc_alloc_interpolate(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         n_weights,    /* I */
  const SYSTEMVALUE rgrWeights[], /* I */
  dl_color_t*   pdlcDest,     /* O */
  dl_color_t* const rgpdlcSrc[]); /* I */

/* \brief Create a new dl color using an existing color to define the paint
 *  mask and the with the given color values (used by shading).
 *
 * \see dlc_alloc_cmd()
 * \see dlc_alloc_fillin()
 * \see dlc_alloc_interpolate()
 */

Bool dlc_alloc_fillin_template(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*   pdlcDest,   /* O */
  const dl_color_t *pdlcSrc,    /* I */
  const COLORVALUE rgcv[],     /* I */
  int32         ccv);       /* I */

/* The following set of functions deal with setting, combining and
   querying overprint information stored with dl colors. For
   straightforward colors, overprints are indicated by simply omitting
   colorants from the dl color; however, when color management
   (interception) is used, small amounts of colorant are introduced
   where there was none before. When this happens we want to switch to
   'maxblt' overprinting, which means rendering the _larger_ of the
   color values from the background and the object rather that all or
   none. This information requires both color value and an indication of
   whether or not the colorant is maxblt-overprinted, and these
   functions provide the interface to that information.  */

enum {DLC_MAXBLT_OVERPRINTS = 1,
      DLC_MAXBLT_KNOCKOUTS} ;

enum {DLC_INTERSECT_OP = 1,
      DLC_UNION_OP,
      DLC_REPLACE_OP} ;

Bool dlc_apply_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         overprintsType, /* I */
  int32         combineOp,      /* I */
  int32         cci,            /* I */
  const COLORANTINDEX rgci [],        /* I */
  dl_color_t*   pdlc);          /* IO */

Bool dlc_doing_maxblt_overprints(
  const dl_color_t * pdlc);       /* I */

Bool dlc_reduce_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t * pdlc);       /* IO */

Bool dlc_clear_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t *dlc);       /* IO */

Bool dlc_colorant_is_overprinted(
  const dl_color_t * pdlc,  /* I */
  COLORANTINDEX ci);        /* I */

/*
 * dl_release() and dlc_release() are the functions to use to directly
 * release a dl color you no longer wish to hang onto but which may be
 * referenced elsewhere.  Your reference will be zeroed out!
 * References to the black and white constant dl colors are silently
 * handled so you don't have to special case your code. dl_release_()
 * and dlc_release_() handle dl colors with ref counts that hit zero.
 */
#define dl_release(context, p_pncolor) \
MACRO_START\
  HQASSERT((p_pncolor != NULL), \
           "dl_release: NULL pointer to n color data pointer"); \
  if ( *(p_pncolor) != NULL ) { \
    /* Not trying to release no-existant color - go ahead and release */ \
    dl_release_(context, p_pncolor); \
  } \
MACRO_END

#define dlc_release(context, pdlc) \
MACRO_START\
  HQASSERT(((pdlc) != NULL), \
           "dlc_release: NULL pointer to dl color entry"); \
  if ( (pdlc)->ce.prc != NULL ) { \
    /* Not trying to release non-existant dl color - go ahead and release */ \
    dlc_release_(context, pdlc); \
  } \
MACRO_END

void dl_release_(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*   p_pncolor);     /* I */

void dlc_release_(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*   pdlc);          /* I */


/*
 * dl_copy() and dlc_copy() allow you to take a copy of a reference to
 * a dl color.  The operation may fail due to the memory copy on the
 * ref count maxing out failing. What also follows on from this is
 * that after a succesful call to ..._copy() the two references may
 * actually point to unique but equal dl colors.  You can copy from
 * the black and white constant dl colors, but not to them.
 */
Bool dl_copy(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*   pp_ncolor_dest,   /* I/O */
  p_ncolor_t*   pp_ncolor_src);   /* I */

Bool dlc_copy(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*   p_dest,           /* I/O */
  dl_color_t*   p_src);           /* I */


/*
 * dl_copy_release() and dlc_copy_release() allow you to effectively
 * hand over a reference to a dl color.  The advantage of these
 * functions is that there is no chance of a memory copy being done so
 * the function is guaranteed to succeed.  The original dl color
 * reference is zeroed out.  You can copy from the black and white
 * constant dl colors, but not to them.
 */
#define dl_copy_release(pp_ncolor_dest, pp_ncolor_src) \
MACRO_START \
  HQASSERT((pp_ncolor_dest != NULL), \
           "dl_copy_release: NULL pointer to destination pointer"); \
  HQASSERT((pp_ncolor_src != NULL), \
           "dl_copy_release: NULL pointer to source pointer"); \
  HQASSERT((*(pp_ncolor_src) != NULL), \
           "dl_copy_release: NULL color data source pointer"); \
  *(pp_ncolor_dest) = *(pp_ncolor_src); \
  *(pp_ncolor_src) = NULL; \
MACRO_END

void dlc_copy_release(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*   pdlcDest,     /* I/O */
  dl_color_t*   pdlcSrc);     /* I */


/*
 * dl_copy_weak() is a reference copy function of last resort.  Only
 * if you need to copy a dl color reference and cannot afford to
 * either lose the original reference or a memory copy failing use
 * this function.  Typically this would be used at render time only
 * when all color creation has been done and memory use is fixed, at
 * other times you may find colors referred to disappearing under your
 * feet
 */
#define dl_copy_weak(pp_ncolor_dest, p_ncolor_src) \
MACRO_START \
  HQASSERT((pp_ncolor_dest != NULL), \
           "dl_copy_weak: NULL pointer to destination pointer"); \
  HQASSERT((p_ncolor_src != NULL), \
           "dl_copy_weak: NULL pointer to source color data"); \
  *(pp_ncolor_dest) = p_ncolor_src; \
MACRO_END

#define dlc_copy_weak(p_dlc_dest, p_dlc_src) \
MACRO_START \
  HQASSERT((p_dlc_dest != NULL), \
           "dlc_copy_weak: NULL pointer to destination pointer"); \
  HQASSERT((p_dlc_src != NULL), \
           "dlc_copy_weak: NULL pointer to source color data"); \
  *(p_dlc_dest) = *(p_dlc_src); \
MACRO_END


/*
 * dl_equal and dlc_equal allow you to test for equality of dl colors
 * Two colors are defined to be equal if they are the same (i.e.
 * p_ncolor1 == p_ncolor2, etc.) or they have the identical paint masks
 * and color values.
 */
Bool dl_equal(
  const p_ncolor_t  p_ncolor1,    /* I */
  const p_ncolor_t  p_ncolor2);   /* I */

Bool dlc_equal(
  const dl_color_t* pdlc1,        /* I */
  const dl_color_t* pdlc2);       /* I */


/*
 * dlc_equal_colorants dl_equal_colorants compare two dl colors for
 * the same colorants (but not necs the same values). Returns true
 * if the colorants are the same and false otherwise.
 */
Bool dlc_equal_colorants(
  const dl_color_t*    pdlc1,      /* I */
  const dl_color_t*    pdlc2);     /* I */

Bool dl_equal_colorants(
 const p_ncolor_t  p_ncolor1,     /* I */
 const p_ncolor_t  p_ncolor2);    /* I */

typedef enum dlc_merge_action_e {
  COMMON_COLORANT_DISALLOW        = 0,
  COMMON_COLORANT_TAKEFROMFIRST   = 1,
  COMMON_COLORANT_AVERAGE         = 2,
  COMMON_COLORANT_MERGEOVERPRINTS = 3
} dlc_merge_action_t;

Bool dl_merge_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*        pp_ncolor1,/* I/O */
  p_ncolor_t*        pp_ncolor2,/* I */
  dlc_merge_action_t action);   /* I */

Bool dlc_merge_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*        pdlc1,     /* I/O */
  dl_color_t*        pdlc2,     /* I */
  dlc_merge_action_t action);   /* I */

Bool dlc_merge_shfill_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*        pdlc1,     /* I/O */
  dl_color_t*        pdlc2,     /* I */
  dlc_merge_action_t action);   /* I */

/*
 * dl_merge and dlc_merge merge the colors together. The routine will
 * fail if the colors have colorants in common.
 */
#define dl_merge(_context, _c1, _c2) \
  dl_merge_with_action(_context, _c1, _c2, COMMON_COLORANT_DISALLOW)
#define dlc_merge(_context, _c1, _c2) \
  dlc_merge_with_action(_context, _c1, _c2, COMMON_COLORANT_DISALLOW)

/*
 * dl_merge_extra and dlc_merge_extra merges the colors
 * together. Should the colors have colorants in common then the
 * colorvalue will be taken from the first color.
 */
#define dl_merge_extra(_context, _c1, _c2) \
  dl_merge_with_action(_context, _c1, _c2, COMMON_COLORANT_TAKEFROMFIRST)

#define dlc_merge_extra(_context, _c1, _c2) \
  dlc_merge_with_action(_context, _c1, _c2, COMMON_COLORANT_TAKEFROMFIRST)

/** dlc_merge_shfill merges shaded fill colors. It expects the channels in each
   color to be the same, but the shfill overprint flags may differ. The channel
   values will be set solid or clear only if both source and destination values
   are solid or clear, otherwise and intermediate value will be selected. The
   overprint flags will be set to the intersection of the overprint flags. */
#define dlc_merge_shfill(_context, _c1, _c2) \
  dlc_merge_shfill_with_action(_context, _c1, _c2, COMMON_COLORANT_AVERAGE)

#define dlc_merge_overprints(_context, _c1, _c2) \
  dlc_merge_shfill_with_action(_context, _c1, _c2, COMMON_COLORANT_MERGEOVERPRINTS)

/** Return max component difference of two colourvalues */
COLORVALUE dlc_max_difference(const dl_color_t * pDlc_1,
                              const dl_color_t * pDlc_2) ;

/** dl_upto_4_common_colorants compares to colors to find any
 * colorants in common, and returns up to 4 of them.
 */
Bool dl_upto_4_common_colorants(
 const p_ncolor_t    p_pncolor1, /* I */
 const p_ncolor_t    p_pncolor2, /* I */
  COLORANTINDEX cis[ 4 ],   /* O */
  int32*        pnci);      /* O */

/*
 * dl_remove_colorant and dlc_remove_colorant create a new color by
 * removing ci from colors. The colorant to be removed must exist in
 * the color.
 */
Bool dl_remove_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*   pp_ncolor_colors,      /* I/O */
  COLORANTINDEX ci);                   /* I */

Bool dlc_remove_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*   pdlc,                  /* I/O */
  COLORANTINDEX ci);                   /* I */


/*
 * dl_common_colorants and dlc_common_colorants check to see if there
 * is intersection between the colors. If there are common colorants
 * then the function returns true and otherwise false.
 */
Bool dl_common_colorants(
 const p_ncolor_t    p_ncolor1,      /* I */
 const p_ncolor_t    p_ncolor2);     /* I */

Bool dlc_common_colorants(
 const dl_color_t*   pdlc1,          /* I */
 const dl_color_t*   pdlc2);         /* I */

/** dl_extra_colorants returns true iff the second color contains
 * colorants which are not present in the first color.
 */
Bool dlc_extra_colorants(
  /*@notnull@*/ /*@in@*/        const dl_color_t*   pdlc1 ,
  /*@notnull@*/ /*@in@*/        const dl_color_t*   pdlc2 ) ;


/** dlc_some_colorant_exists() checks to see if one or more of the
 * colorant indexes is present in the dl color.
 */
Bool dlc_some_colorant_exists(
  /*@notnull@*/ /*@in@*/        const dl_color_t*     pdlc ,
  /*@notnull@*/ /*@in@*/        const COLORANTINDEX   rgci[]) ;


/**
 * dlc_set_indexed_colorant() and dlc_get_indexed_colorant() are the
 * means to set and get the current active colorant (which also may
 * be set by the iterator functions below).  You should have always
 * done a call to an iterator or dlc_set_indexed_colorant() before
 * calling dlc_get_indexed_colorant() - typical code may look like
 *
 * \code
 *   if ( dlc_set_indexed_colorant(pdlc, ci) ) {
 *      dlc_get_indexed_colorant(pdlc, ci, &cv);
 *      ...
 *   }
 * \endcode
 *
 * Note that even though they both have "const" qualifiers on the dlc pointer,
 * they do modify the cached index and colourvalue in the dlc. We attempt to
 * justify this by noting that the observed external behaviour is the same
 * regardless of whether the data was cached, and by noting that we never
 * declare any dlc's constant (they may be accessed through const-qualified
 * pointers because a structure in which the dlc is contained is not
 * supposed to be modified by callers).
 */
Bool dlc_set_indexed_colorant(
  /*@notnull@*/ /*@in@*/        const dl_color_t*     pdlc ,
                                COLORANTINDEX   ci ) ;

#define dlc_get_indexed_colorant(pdlc, index, pcv) \
    (((pdlc)->ci == (index)) \
      ? (*(pcv) = (pdlc)->cv, TRUE) \
      : dlc_get_indexed_colorant_(pdlc, index, pcv))

Bool dlc_get_indexed_colorant_(
  /*@notnull@*/ /*@in@*/        const dl_color_t*     pdlc ,
                                COLORANTINDEX   ci ,
  /*@notnull@*/ /*@out@*/       COLORVALUE*     pcv ) ;


/** dlc_replace_indexed_colorant() allows you to create a new dl color
 * with a single colorants value replaced.
 */
Bool dlc_replace_indexed_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/                 dl_color_t*     pdlc ,
  COLORANTINDEX   ci ,
  COLORVALUE      cv ) ;


/** Change a color to use a different set of colorants, but with the same
    color values. Colorant indices are remapped according to the colorant
    map. This is used to convert preseparated pseudo colorants to real
    colorants prior to backdrop rendering.

    \param[in] incolor  The input color to remap.
    \param[out] oncolor A location where the remapped color will be stored.
    \param[in] map      An input mapping from COLORANTINDEX to COLORANTINDEX.
    \param maplength    The number of elements in the \a map array.
 */
Bool dl_remap_colorants(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@in@*/ p_ncolor_t incolor,
  /*@notnull@*/ /*@out@*/ p_ncolor_t *oncolor,
  /*@notnull@*/ /*@in@*/ const COLORANTINDEX *map,
  int32 maplength);

/** dlc_indexed_colorant_present() allows you to check for the presence
 * of an indexed colorant in a dl color entry without changing the
 * cached color index and value (as in dlc_set_indexed_colorant())
 */
Bool dlc_indexed_colorant_present(
  /*@notnull@*/ /*@in@*/        const dl_color_t*     pdlc ,
                                COLORANTINDEX   ci ) ;


/** Class of dl color entry color - black or white constant, or tint values
 */
typedef int32 dlc_tint_t;

enum {
  DLC_TINT_BLACK = 1,
  DLC_TINT_WHITE,
  DLC_TINT_OTHER
} ;

/** \brief Check for special dl color entries all0 and all1 for black or
 * white.
 *
 * \param[in] pdlc      Pointer to dl color entry
 *
 * \retval DLC_TINT_BLACK if the current colorant is black
 * \retval DLC_TINT_WHITE if it is white
 * \retval DLC_TINT_OTHER if not black or white.
 *
 * Typical code may look like -
 *
 * \code
 *    tint = dlc_check_black_white(pdlc);
 *    switch ( tint ) {
 *    case DLC_TINT_BLACK: ...; break;   - handle black color case
 *    case DLC_TINT_WHITE: ...; break;   - handle white color case
 *    case DLC_TINT_OTHER: ...; break;   - handle general case (may still be black/white)
 *    }
 * \endcode
 *
 * \note NONE dl color objects are covered as DLC_TINT_OTHER!
 */
dlc_tint_t dlc_check_black_white(
  /*@notnull@*/ /*@in@*/        const dl_color_t*   pdlc ) ;


/** dlc_has_allsep() checks a dl color entry to see if the colro
 * was defined for the /All separation.
 */
Bool dlc_has_allsep(
  /*@notnull@*/ /*@in@*/        const dl_color_t*   pdlc ) ;


/*
 * dlc_first_colorant() and dlc_next_colorant() provide iterator functionality
 * over all the colorants present in a dl color.
 *
 * Since a dl color can take many forms you may need to check the result of the
 * iterator call to decide how to proceed.  Code may typically look like -
 *
 * \code
 *   rc = dlc_first_colorant(pdlc, &iter, &ci, &cv);
 *   while ( rc == DLC_ITER_COLORANT ) {
 *     ...
 *     rc = dlc_next_colorant(pdlc, &iter, &ci, &cv);
 *   }
 *   switch ( rc ) {
 *   case DLC_ITER_ALLSEP: ...; break;  - /All sep color value
 *   case DLC_ITER_ALLO1:  ...; break;  - black or white constant
 *   case DLC_ITER_NONE:   ...; break;  - /None sep dl color (no color values)
 *   case DLC_ITER_NOMORE: ...; break;  - no more colorants in dl color
 *   }
 * \endcode
 *
 * Note that even though they both have "const" qualifiers on the dlc pointer,
 * they do modify the cached index and colourvalue in the dlc. We attempt to
 * justify this by noting that the observed external behaviour is the same
 * regardless of whether the data was cached, and by noting that we never
 * declare any dlc's constant (they may be accessed through const-qualified
 * pointers because a structure in which the dlc is contained is not
 * supposed to be modified by callers).
 */

/** Iterator state context for iterating over colorants in dl color entry
 */
typedef struct dl_color_iter_t {
  paint_mask_t* ppmLast;        /* Pointer to last paint mask with colorant bit */
  uint32        pmLast;         /* Last paint mask used to check for colorant bit */
  int32         ciLast;         /* Colorant index of last colorant */
  COLORVALUE*   pcvLast;        /* Pointer to last colorant value */
} dl_color_iter_t;

/**
 * Return states from colorant iterator as follows
 *
 * NOMORE     - no more colorants in chain
 * COLORANT   - got a unique colorant
 * ALLSEP     - got colorant on allsep wild card match
 * ALL01      - no color values as is ALL0 or ALL1 paint mask
 * NONE       - NONE colorant index, no color values
 */
typedef int32 dlc_iter_result_t;

enum {
  DLC_ITER_NOMORE = 1,
  DLC_ITER_COLORANT,
  DLC_ITER_ALLSEP,
  DLC_ITER_ALL01,
  DLC_ITER_NONE
} ;

dlc_iter_result_t dlc_first_colorant(
  /*@notnull@*/ /*@in@*/        const dl_color_t* pdlc ,
  /*@notnull@*/ /*@out@*/       dl_color_iter_t*  pdci ,
  /*@notnull@*/ /*@out@*/       COLORANTINDEX*    pci ,
  /*@notnull@*/ /*@out@*/       COLORVALUE*       pcv ) ;

dlc_iter_result_t dlc_next_colorant(
  /*@notnull@*/ /*@in@*/        const dl_color_t* pdlc ,
  /*@notnull@*/ /*@in@*/        dl_color_iter_t*  pdci ,
  /*@notnull@*/ /*@out@*/       COLORANTINDEX*    pci ,
  /*@notnull@*/ /*@out@*/       COLORVALUE*       pcv ) ;


/*
 * dlc_get_black() and dlc_get_white() are the functions to use to pick
 * up the constant black and white dl color entries.  They do not have to
 * given up with a call to dlc_release() or dl_release().
 */
void dlc_get_black(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc) ;

void dlc_get_white(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc) ;

void dlc_get_none(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc) ;

/** dl_to_black release the existing dl color and update to the black ncolor.
 */
void dl_to_black(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor) ;

/** dl_to_white release the existing dl color and update to the white ncolor.
 */
void dl_to_white(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor) ;

/** dl_to_none release the existing dl color and update to the none ncolor.
 */
void dl_to_none(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor) ;

/*
 * dl_is_none() and dlc_is_none() test for the ncolor and dl
 * color being for the NONE separation.
 */
Bool dl_is_none(
  /*@notnull@*/ /*@in@*/        const p_ncolor_t      p_ncolor ) ;

Bool dlc_is_none(
  /*@notnull@*/ /*@in@*/        const dl_color_t*     pdlc ) ;


/** \brief Test is a DL color is black.

    \param[in] dlc    The DL color to test.
    \param blackIndex A colorant index indicating the index of black in the DL
                      color's colorspace. If this is \c COLORANTINDEX_ALL, the
                      colorspace is additive and all channels must be zero. If
                      it is \c COLORANTINDEX_NONE, then no channel represents
                      black and only the special black test will be performed.

    \retval TRUE if the passed color is black, either because of special color
         commands or because the black indices are zero.
    \retval FALSE if the color is not special black and the colorant index
         cannot be used to determine if the color is black.
 */
Bool dlc_is_black(/*@notnull@*/ /*@in@*/ const dl_color_t *dlc,
                  COLORANTINDEX blackIndex) ;

/** \brief Test is a DL color is white.

    \param[in] dlc  The DL color to test.

    \retval TRUE if the passed color is white, either because of special color
            commands or because all components are one.
    \retval FALSE if the color is not white.
 */
Bool dlc_is_white(/*@notnull@*/ /*@in@*/ const dl_color_t* dlc);

dl_color_t *dlc_currentcolor(dlc_context_t *context);

COLORVALUE dl_currentopacity(dlc_context_t *context);
void dl_set_currentopacity(dlc_context_t *context, COLORVALUE opacity);

uint8 dl_currentspflags(dlc_context_t *context);
void dl_set_currentspflags(dlc_context_t *context, uint8 spflags);

GSC_BLACK_TYPE dl_currentblacktype(dlc_context_t *context);
void dl_set_currentblacktype(dlc_context_t *context, GSC_BLACK_TYPE blacktype);

/** dl_num_channels returns the number of channels in the dl color.
 */
int32 dl_num_channels(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor ) ;


/*
 * dlc_from_dl() and dlc_from_lobj() are functions to create dl color
 * entries from n color data pointers and LISTOBJECTs respectively.
 * These are reference counting functions so may fail with VMERROR.
 */

Bool dlc_from_dl(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@in@*/        p_ncolor_t*   pp_ncolor ,
  /*@notnull@*/ /*@out@*/       dl_color_t*   pdlc ) ;

Bool dlc_from_lobj(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@in@*/        LISTOBJECT*   p_lobj ,
  /*@notnull@*/ /*@out@*/       dl_color_t*   pdlc ) ;


/*
 * dlc_from_lobj_weak() and dlc_from_dl_weak() create dl color entries
 * without reference counting, and is mainly for render code when the
 * display list has been built and the references are known to be
 * transitory.  With these functions you cannot rely on the
 * references not changing under your feet.
 */
void dlc_from_dl_weak(
  /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor ,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc) ;

#define dlc_from_lobj_weak(p_lobj, pdlc) \
MACRO_START \
  HQASSERT(((p_lobj) != NULL), \
           "dlc_from_lobj_weak: NULL pointer to dl object"); \
  dlc_from_dl_weak((p_lobj)->p_ncolor, pdlc); \
MACRO_END


/*
 * dlc_to_lobj() and dlc_to_dl() provide a means to assign a dl color
 * entry to a LISTOBJECT and a color data pointer respectively, with
 * reference counting, so they may fail with a VMERROR.
 */

Bool dlc_to_lobj(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/       LISTOBJECT*   p_lobj ,
  /*@notnull@*/ /*@in@*/        dl_color_t*   pdlc ) ;

Bool dlc_to_dl(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/       p_ncolor_t*   pp_ncolor ,
  /*@notnull@*/ /*@in@*/        dl_color_t*   pdlc ) ;


/*
 * dlc_to_dl_weak() assigns a dl color entry to a color data pointer
 * without reference counting. This means that the dl color can change
 * under your feet - mainly for render time.
 */

#define dlc_to_dl_weak(pp_ncolor, pdlc) \
MACRO_START \
  HQASSERT((pp_ncolor != NULL), \
           "dlc_to_dl_weak: NULL pointer to n color pointer"); \
  HQASSERT((pdlc != NULL), \
           "dlc_to_dl_weak: NULL pointer to dl color entry"); \
  HQASSERT(((pdlc)->ce.prc != NULL), \
           "dlc_to_dl_weak: NULL pointer to dl color entry data"); \
  *(pp_ncolor) = (p_ncolor_t)((pdlc)->ce.prc); \
MACRO_END


/** dlc_to_lobj_release() allows you to assign a dl color to a
 * LISTOBJECT and hand over the reference you have.  Since no
 * new reference is being created this function is guaranteed to
 * succeed.
 */

void dlc_to_lobj_release(
  /*@notnull@*/ /*@out@*/       LISTOBJECT*   p_lobj ,
  /*@notnull@*/ /*@in@*/        dl_color_t*   pdlc ) ;


/** Make a dl color containing the full set of fully-fledged colorants
 * from the raster style with all the values set to cvInit.
 */
Bool dlc_from_rs(DL_STATE *page, GUCR_RASTERSTYLE *rs,
                 dl_color_t *dlc, COLORVALUE cvInit);

/* dl_color_opacity returns the opacity (aka alpha) associated with a dl color.
   Normally the value returned will be COLORVALUE_ONE, meaning opaque.  (Used
   for gradients with an opacity channel.) */
COLORVALUE dl_color_opacity(const p_ncolor_t ncolor);
COLORVALUE dlc_color_opacity(const dl_color_t* dlcolor);


#endif /* !__DL_COLOR_H__ */


/* Log stripped */
