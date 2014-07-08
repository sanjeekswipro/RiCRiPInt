/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_color.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file provides a set of functions to manipulate ncolor entries.  The
 * functions can be broken into 3 data groupings - paint masks (prefixed with
 * pm_) color entry (ce_ prefix), and display list color (dlc_ prefix)
 */

#include "core.h"
#include "blitcolort.h" /* BLIT_MAX_COLOR_CHANNELS */
#include "swerrors.h"   /* For VMERROR */
#include "hqmemcpy.h"
#include "hqmemset.h"
#include "monitor.h"
#include "tables.h"     /* highest_bit_set_in_byte, etc */
#include "lists.h"
#include "debugging.h"
#include "namedef_.h"

#include "constant.h"   /* for EPSILON */
#include "dl_color.h"
#include "display.h"
#include "dlstate.h"
#include "gu_chan.h"    /* GUCR_COLORANTINDEX_CACHE */
#include "hqbitops.h"   /* INLINE_MINMAX32 */
#include "swrle.h"      /* RLE_MASK_RECORD_TYPE */
#include "objnamer.h"
#include "lowmem.h"

/*
 * Control whether to include instrumentaion code in build but
 * only debug builds
 */
#if defined( DEBUG_BUILD )
#undef DLC_INSTRUMENT
#endif


/* ------------- Paint mask types --------------- */

/**
 * Macro to calculate padded size for required alignment
 */
#define DLC_ALIGNSIZE(s, a) (((s) + (a) - 1)/(a))*(a)

/**
 * Macro to calc color data header size (consisting of ref count and paint masks)
 * suitably aligned for the following colorvalues.
 */
#define DLC_HEADERSIZE(cbpm)  (DLC_ALIGNSIZE((sizeof(ref_count_t) + (cbpm)), sizeof(COLORVALUE)))

/**
 * Current mm alignment size - hopefully to be changed to 4 or even 2 soon!
 * Changed from 8 bytes to 4 bytes on 1 July 1998.
 */
#define DLC_MM_ALIGNSIZE    (4)

/**
 * Number of colorant bits per paint mask byte.
 */
#define PM_COLORANT_BITS    (7)


/**
 * Bit mask to identify the paint mask chaining bit, if it is set then
 * there is another paint mask byte after this one.
 */
#define PM_CHAIN    ((paint_mask_t)0x80)


/*
 * Macros to abstract manipulating paint mask chain bit
 */
#define PM_SET_CHAIN(b)     MACRO_START (b) |= PM_CHAIN; MACRO_END
#define PM_CLEAR_CHAIN(b)   MACRO_START (b) &= ~PM_CHAIN; MACRO_END
#define PM_CHAIN_SET(b)     (((b) & PM_CHAIN) == PM_CHAIN)
#define PM_GET_MASK(b)      ((paint_mask_t)((b) & ~PM_CHAIN))

/**
 * Macro to test for colorant bit index present in paint mask byte
 */
#define PM_COLORANT_SET(pm, i)  (((pm) & grgpmMask[(i)]) != 0)


/*
 * Macros to convert absolute colorant indexes to pm index and to index of bit in
 * pm byte. Indexes are 0 (zero) based.
 *
 * Note: colorant bits are in MSB->LSB order, so index order is ..., 1, 0.
 */
#define PM_INDEX(colorant)      ((colorant)/PM_COLORANT_BITS)
#define PM_BIT_INDEX(colorant)  (PM_COLORANT_BITS - (colorant)%PM_COLORANT_BITS - 1)

#define PM_FIRST_COLORANT       ((paint_mask_t)(1 << (PM_COLORANT_BITS - 1)))

/*
 * Paint mask command codes. A command code is introduced by a zero paint
 * mask byte. The following byte indicates the command and may also includes
 * a chain bit to allow more than one command per color.
 *  ALL0     - 0 color value in all colorant channels
 *  ALL1     - 1(max) color value in all colorant channels
 *  ALLSEP   - special handling for objects rendered into /All separation
 *  NONE     - special dl color entry for NONE separation
 *  OPMAXBLT - command is followed by second set of paint masks indicating max-blits
 *  OPACITY  - opacity is the final color value; if absent the dl color is opaque
 */
#define PM_CMD          ((paint_mask_t)0x00)
enum {
  PM_CMD_FIRST = 0x0F,
  PM_CMD_ALL0,
  PM_CMD_ALL1,
  PM_CMD_ALLSEP,
  PM_CMD_NONE,
  PM_CMD_OPMAXBLT,
  PM_CMD_OPACITY,
  PM_CMD_LAST
};

/*
 * Paint mask command code test macros.
 */
#define PM_IS_CMD_ALL0(pm)      (PM_GET_MASK(pm) == PM_CMD_ALL0)
#define PM_IS_CMD_ALL1(pm)      (PM_GET_MASK(pm) == PM_CMD_ALL1)
#define PM_IS_CMD_ALLSEP(pm)    (PM_GET_MASK(pm) == PM_CMD_ALLSEP)
#define PM_IS_CMD_NONE(pm)      (PM_GET_MASK(pm) == PM_CMD_NONE)
#define PM_IS_CMD_OPMAXBLT(pm)  (PM_GET_MASK(pm) == PM_CMD_OPMAXBLT)
#define PM_IS_CMD_OPACITY(pm)   (PM_GET_MASK(pm) == PM_CMD_OPACITY)
#define PM_IS_VALID_CMD(pm)     (PM_GET_MASK(pm) > PM_CMD_FIRST && \
                                 PM_GET_MASK(pm) < PM_CMD_LAST)

/*
 * Define sizes of paint masks and commands in terms of bytes.
 */
#define PM_SIZE         (sizeof(paint_mask_t))
#define PM_SIZE_CMD     (2*PM_SIZE)


/*
 * Array to map colorant paint mask bit index to actual bit
 */
static paint_mask_t grgpmMask[7] = {
  (PM_FIRST_COLORANT >> 6),
  (PM_FIRST_COLORANT >> 5),
  (PM_FIRST_COLORANT >> 4),
  (PM_FIRST_COLORANT >> 3),
  (PM_FIRST_COLORANT >> 2),
  (PM_FIRST_COLORANT >> 1),
  PM_FIRST_COLORANT
};


/*
 * Arrays of complete paint mask bits before and after (lead/trail) a colorant
 * bit index.
 */
static paint_mask_t grgpmMaskLead[7] = {
  0x7e, 0x7c, 0x78, 0x70, 0x60, 0x40, 0
};
static paint_mask_t grgpmMaskTrail[7] = {
  0, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f
};

/*
 * Type for types of paint mask
 */
typedef int32 PM_CMD_TYPE;

/** Types of paint mask. */
enum {
  PM_PAINTMASK = 1,
  PM_BLACK,
  PM_WHITE,
  PM_NONE
} ;

/** This definition of the opaque ncolor struct does not reflect the true
    contents of the structure, it exists to make the debugging addin work.

    DL colors are stored as a contiguous block of memory, referred to be
    a \c p_ncolor_t, which is a typedeffed pointer to this structure. They
    are unpacked into \c color_entry_t by \c ce_from_ncolor() before doing
    anything with them. The structure of the packed color is a 2-byte
    reference count, followed by a variable-length paint mask.

    The paintmask has the grammar:

    pm = PM_CMD PM_CMD_ALL0 |
         PM_CMD PM_CMD_ALL1 |
         PM_CMD PM_CMD_NONE |
         (
           ( Mask byte 0x00-0x7f[chain] ) +
           ( PM_CMD PM_CMD_ALLSEP[chain] ) ?
           ( PM_CMD PM_CMD_OPACITY[chain] ) ?
           ( PM_CMD PM_CMD_OPMAXBLT ( Mask byte 0x00-0x7f[chain] ) + ) ?
           ( one byte padding to align to 16-bits ) ?
           16-bit colorvalues {repeat for each mask bit set, allsep, and alpha}
         )

    The commands and mask bytes marked [chain] have the top bit set if
    followed by another command or mask byte. The masks for colorant indices
    and maxblit indices have 7 bits per byte indicating if the colorant
    corresponding to the bit is present. The colorant masks are interpreted
    high bit first, so colorant index 0 will correspond to a mask byte of
    0x40.

    When there is a PM_CMD_ALLSEP present, the last paintmask byte is padded
    with the All separation value, and extra colorvalues are inserted. This
    behaviour exists for historical reasons, and could be removed once we've
    checked that all appropriate routines fall back to using the All separation
    if a colorant is not present.
*/
struct p_ncolor_t_opaque {
  ref_count_t refs ;   /* Reference count */
  paint_mask_t pm[1] ; /* Extendable struct; pm followed by colorvalues. */
} ;

/* ------------- DL color cache types --------------- */

/*
 * Some largish primes for cache size - 1021, 2039, 4093, and 8191
 * Use a smaller cache size in debug builds to increase churning and
 * therefore test cache overflow handling code.
 */
uint32 dlc_cache_size()
{
  if ( low_mem_configuration() ) {
    return 131;
  } else {
#if defined( DEBUG_BUILD )
  return 1021u;
#else
  return 2039u;
#endif
  }
}

#ifdef ASSERT_BUILD
static Bool dlc_hash_valid(uint32 h)
{
  return (h < dlc_cache_size());
}
#endif

/**
 * The cache entry - a link element for the hash overflow chain,
 * the actual cached color entry, and the mru list for the dl color
 * (see below).
 */
typedef struct dlc_cache_entry_t {
  dll_link_t              dll;      /* Link to hash overflow chain */
  color_entry_t           ce;       /* Color entry wrapper for dl color */
  struct dlc_mru_entry_t *pdme;     /* Pointer to active cache entry list entry */
} dlc_cache_entry_t;


/**
 * The MRU list entry - a link element to maintain the MRU list,
 * and a pointer to the cache entry concerned.
 */
typedef struct dlc_mru_entry_t {
  dll_link_t          dll;      /* Link within the MRU list */
  dlc_cache_entry_t*  pdce;     /* Pointer to the cache entry */
} dlc_mru_entry_t;


#ifdef DLC_INSTRUMENT

/**
 * Structure to hold useful cache instrumentation counts
 */
typedef struct dlc_instrument_cache_s {
  int32  cAdd;      /* Count of dl color added to the cache */
  int32  cHit;      /* Count of dl color cache hits */
  int32  cRemove;   /* Count of attempts to remove colors from cache */
  int32  cLost;     /* Count of colors lost from cache */
  int32  cFound;    /* Count of colors with a zero ref count resurrected */
} dlc_instrument_cache_t;

#endif /* DLC_INSTRUMENT */


/* ------------- General instrumentation types --------------- */


#ifdef DLC_INSTRUMENT

/**
 * Structure to hold useful memory handling instrumentation
 */
typedef struct dlc_instrument_mem_s {
  int32 cAllocated;     /* Number of times a color is allocated in VM */
  int32 cbAllocated;    /* Total memory allocated for dl colors */
  int32 cReleased;      /* Number of times color freed on release */
  int32 cbReleased;     /* Total dl color memory freed */
  int32 cReUse;         /* Number of times freed memory can be reused */
} dlc_instrument_mem_t;

/**
 * Structure to hold useful reference counting instrumentation
 */
typedef struct dlc_instrument_refs_s {
  int32 cCreated;       /* Number of colors created */
  int32 cGetReference;  /* Number of calls to dlc_get_reference */
  int32 cReUse;         /* Number of times can use exisiting n color */
  int32 cCopyOnMax;     /* Number of times copied on ref count maxing out */
  int32 rcMax;          /* Max ref count achieved for a single color */
} dlc_instrument_refs_t;

#endif /* DLC_INSTRUMENT */


/* ------------- DL color context structure --------------- */

/*
 * Macros to inline common test for constant black or white colors.
 */
#define DLC_IS_CONSTANT(context, pdlc) \
  ((pdlc) == &(context)->dlc_white || \
   (pdlc) == &(context)->dlc_black || \
   (pdlc) == &(context)->dlc_none)

#define DLC_REFS_CONSTANT(context, pdlc) \
  ((pdlc)->ce.prc == (context)->dlc_white.ce.prc || \
   (pdlc)->ce.prc == (context)->dlc_black.ce.prc || \
   (pdlc)->ce.prc == (context)->dlc_none .ce.prc)

#define DL_REFS_CONSTANT(context, p_ncolor) \
  (&(p_ncolor)->refs == (context)->dlc_white.ce.prc || \
   &(p_ncolor)->refs == (context)->dlc_black.ce.prc || \
   &(p_ncolor)->refs == (context)->dlc_none .ce.prc)

/*
 * A 2KB buffer to initially construct new dl colors in.  They are
 * then compared with the entires in the dl color cache - if already
 * present it is ignored, otherwise memory is allocated and the already
 * built dl color is copied to it.
 */
#define DLC_BUFFER_SIZE   (2048)

struct dlc_context_t {
  /* Workspace for constructing dl colors */
  uint8 *grgbColorBuffer;

  /*
   * The three lists used to track dl colors in the cache.
   * gdlsMRURef     - list of actively referenced dl colors in the cache
   * gdlsMRUUnRef   - list of unreferenced dl colors in the cache
   * gdlsFree       - list of unused dl cache entries
   *
   * The idea is that when adding a new dl color to the cache we
   * first take entries from the free list, then from the least
   * recently used unreferenced dl color list, and finally from the
   * least recently used referenced dl color list.
   */
  dll_list_t gdlsMRURef, gdlsMRUUnRef, gdlsFree;

  /**
   * This is the cache proper - hash values are indexes into this array of
   * 2-way linked lists of color entries.
   */
  dll_list_t *grgdlsCache; /* dlc_cache_size() */

  /**
   * The array of dl color cache entries
   */
  dlc_cache_entry_t *grgdce; /* 2*dlc_cache_size() */

  /**
   * Sufficient MRU list entries for cache entries
   */
  dlc_mru_entry_t *grgdme; /* 2*dlc_cache_size() */

  /*
   * Cached pointer and size of last dl color data attempted to be
   * freed.  Frequently the next unique color allocated wants the same
   * size memory as the last one freed, so this saves on mm calls to
   * both free and allocate.  Very good for shaded fills at high res.
   */
  p_ncolor_t gp_ncolorSave;  /* Pointer to last memory pseudo freed */
  mm_size_t  gcbSave;        /* Size of last memory block pseudo freed */

#ifdef DLC_INSTRUMENT
  dlc_instrument_cache_t gdic; /**< cache instrumentation counts */

  dlc_instrument_mem_t gdim; /**< memory handling instrumentation */

  dlc_instrument_refs_t gdir; /**< reference counting instrumentation */
#endif /* DLC_INSTRUMENT */

  mm_pool_t *pools; /**< For all dl color allocations; currently set to page->dlpools */

  dl_color_t dlc_black, dlc_white, dlc_none; /**< Constant black, white and none dl colors */

  dl_color_t dlc_currentcolor; /**< The result of the last gsc_invokeChainSingle */
  COLORVALUE currentopacity; /**< The current value of opacity (for shfills) */
  uint8 currentspflags; /**< The current set of RENDER_RECOMBINE/PATTERN/KNOCKOUT flags */
  GSC_BLACK_TYPE currentblacktype; /**< The result blackType usually inherited when compositing */

  OBJECT_NAME_MEMBER
};

#define DLC_ALIGN_UP(x) PTR_ALIGN_UP_P2(int32, x, sizeof(uintptr_t))

/* hacky change to convert dlc_context_t into having run-time rather than
 * compile-time sized arrays within in.
 * Now contains :-
 *   dll_list_t *grgdlsCache; size dlc_cache_size()
 *   dlc_cache_entry_t *grgdce; size 2*dlc_cache_size()
 *   dlc_mru_entry_t grgdme; size 2*dlc_cache_size()
 */
int32 sizeof_dlc_context_t()
{
  int32 sz = DLC_ALIGN_UP(sizeof(dlc_context_t));

  sz += dlc_cache_size() * DLC_ALIGN_UP(sizeof(dll_list_t));
  sz += 2*dlc_cache_size() * DLC_ALIGN_UP(sizeof(dlc_cache_entry_t));
  sz += 2*dlc_cache_size() * DLC_ALIGN_UP(sizeof(dlc_mru_entry_t));

  return sz;
}

/**
 * Types of special command dl colors that can be created
 */
enum {
  DLC_WHITE = 1,
  DLC_BLACK,
  DLC_NONE
} ;


#if defined(DEBUG_BUILD) || defined(VALGRIND_BUILD)
static void debug_scribble_buffer(dlc_context_t *context, int num_colorants);
#else
#define debug_scribble_buffer(context, num_colorants)
#endif

/* ------------- Private n color data functions --------------- */

static Bool dlc_alloc_cmd(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         nCmd,         /* I */
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc);


/** \brief Handles memory allocation for dl color data.
 *
 * \param[in] context DL color context.
 * \param[in] cb      Amount of memory required in bytes suitably aligned.
 * \param[out] pp_ncolor  Pointer to returned pointer to allocated memory.
 * \retval TRUE       Returned if memory allocation succeeded.
 * \retval FALSE      Returned if memory allocation failed.
 *
 * \note
 * Calls error handler with VMERROR if allocation fails If need to handle low
 * memory better then this is the only place that needs updating.
 */
static Bool ncolor_alloc(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  mm_size_t     cb,         /* I */
  p_ncolor_t*   pp_ncolor)  /* I/O */
{
  HQASSERT(cb > 0, "trying to allocate zero or negative number of bytes");
  HQASSERT(cb%DLC_MM_ALIGNSIZE == 0, "unalligned amount of bytes");
  HQASSERT(pp_ncolor != NULL, "NULL pointer to returned pointer");

  if ( cb != context->gcbSave ) {
    /* Cannot use last freed memory - alloc new memory */

#ifdef DLC_INSTRUMENT
    /* Count of dl colors allocated */
    context->gdim.cAllocated++;
    /* Count of memory allocated */
    context->gdim.cbAllocated += cb;
#endif /* DLC_INSTRUMENT */

    /* Try to allocate required memory */
    *pp_ncolor = dl_alloc(context->pools, cb, MM_ALLOC_CLASS_NCOLOR);

  } else { /* Can use last freed memory */

#ifdef DLC_INSTRUMENT
    /* Count number of times freed memory reused */
    context->gdim.cReUse++;
#endif /* DLC_INSTRUMENT */

    *pp_ncolor = context->gp_ncolorSave;
    context->gp_ncolorSave = NULL;
    context->gcbSave = 0;
  }

  return (*pp_ncolor != NULL || error_handler(VMERROR));

} /* Function ncolor_alloc */


/** \brief Handles freeing of memory for dl color data.
 *
 * \param[in] context DL color context.
 * \param[in] cb     Amount of memory to release in bytes suitably aligned.
 * \param[out] pp_ncolor  Pointer to returned pointer to memory to free.
 */
static void ncolor_free(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  mm_size_t     cb,             /* I */
  p_ncolor_t*   pp_ncolor)      /* I/O */
{
  HQASSERT(cb > 0, "trying to free zero or negative number of bytes");
  HQASSERT(cb%DLC_MM_ALIGNSIZE == 0, "trying to free unalligned amount of bytes");
  HQASSERT(pp_ncolor != NULL, "NULL pointer to pointer to memory to free");
  HQASSERT(*pp_ncolor != NULL, "NULL pointer to memory to free");

  if ( context->gp_ncolorSave != NULL ) {
    /* Already hanging onto freed color - finally free it */

#ifdef DLC_INSTRUMENT
    /* Count number of dl colors freed */
    context->gdim.cReleased++;
    /* Count of memory allocated */
    context->gdim.cbReleased += context->gcbSave;
#endif /* DLC_INSTRUMENT */

    /* Free off old dl color entry */
    dl_free(context->pools, context->gp_ncolorSave, context->gcbSave,
            MM_ALLOC_CLASS_NCOLOR);
  }

#if defined( DEBUG_BUILD )
  /* Splat dl color data with tell-tale */
  HqMemSet8((uint8 *)*pp_ncolor, 0xaa, cb);
#endif

  /* Remember n color memory being freed */
  context->gp_ncolorSave = *pp_ncolor;
  context->gcbSave = cb;

  /* Reset pointer */
  *pp_ncolor = NULL;

} /* Function ncolor_free */


/* ------------- Private paint mask functions --------------- */

#if defined( ASSERT_BUILD )

/** \brief Check array of colorant indexes are in ascending order.
 *
 * \param cci         Number of indexes.
 * \param[in] rgci    Array of colorant indexes.
 *
 * \retval TRUE       If indexes are in ascending order.
 * \retval FALSE      If indices are not in ascending order.
 */
static Bool pm_in_order(
  int32         cci,        /* I */
  const COLORANTINDEX rgci[])    /* I */
{
  Bool fInOrder = TRUE;
  int32 ici;

  HQASSERT(cci > 0, "zero sized array");
  HQASSERT(rgci != NULL, "NULL pointer to index array");

  for ( ici = 0; fInOrder && ici < cci - 1; ici++ ) {
    fInOrder = rgci[ici] < rgci[ici + 1];
  }

  return fInOrder;
} /* Function pm_in_order */

#endif

/** \brief Test type of paint mask currently pointed to.
 *
 * \param[in] pm      Array of paint mask bytes to test.
 *
 * \returns           Type of paint mask bytes at start of array.
 */
static PM_CMD_TYPE pm_special_type(
            /*@notnull@*/ /*@in@*/ const paint_mask_t pm[])
{
  PM_CMD_TYPE   cmd_type;

  if ( pm[0] == PM_CMD ) {
    /* Got special command code - decide which */

    switch ( pm[1] ) {
    case PM_CMD_ALL0:
      cmd_type = PM_BLACK;
      break;

    case PM_CMD_ALL1:
      cmd_type = PM_WHITE;
      break;

    case PM_CMD_NONE:
      cmd_type = PM_NONE;
      break;

    default:
      HQFAIL("pm_special_type: unknown command code found");
      cmd_type = PM_PAINTMASK;
      break;
    }

  } else { /* Paint mask has colorant list */
    cmd_type = PM_PAINTMASK;
  }

  return cmd_type;

} /* Function pm_special_type */


/** \brief Find length of paint mask in bytes.
 *
 * \param[in] rgpm  Array of paint mask bytes.
 *
 * \returns         Count of paint mask bytes, always greater than 0!
 *
 * \note
 * Doesn't take account of possible mm padding, should be done by caller.
 * Assumes paint masks are bytes - need to change if otherwise.
 */
static int32 pm_findsize(
  /*@notnull@*/ /*@in@*/ const paint_mask_t rgpm[])
{
  int32 cb;
  int32 ipm = -1;

  HQASSERT(rgpm != NULL, "NULL paint mask byte array address");

  if ( rgpm[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */
    do {
      ++ipm;
    } while ( PM_CHAIN_SET(rgpm[ipm]) && rgpm[ipm + 1] != PM_CMD );

    if ( PM_CHAIN_SET(rgpm[ipm]) &&
         rgpm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(rgpm[ipm + 2])) {
      /* All sep present */
      ipm += 2;
    }

    if ( PM_CHAIN_SET(rgpm[ipm]) &&
         rgpm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(rgpm[ipm + 2])) {
      /* Opacity is present */
      ipm += 2;
    }

    if ( PM_CHAIN_SET(rgpm[ipm]) &&
         rgpm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(rgpm[ipm + 2])) {
      ipm += 2;
      /* Note: it is possible to have a valid paint mask byte of zero at the end
         here, and it is not a CMD. This is because colorants may have been added
         in the relevant byte but then removed afterwards. */
      do {
        ++ipm;
      } while ( PM_CHAIN_SET(rgpm[ipm]) /* && (rgpm[ipm + 1] != PM_CMD) */);
    }
  } else { /* Got special command code (all0, all1 or none - all the same size) */
    ipm += PM_SIZE_CMD;
  }
  HQASSERT(!PM_CHAIN_SET(rgpm[ipm]), "A dl color paint mask terminated early");

  cb = (ipm + 1) * PM_SIZE;

  return cb;

} /* Function pm_findsize */

/** \brief Find length of paint mask in bytes, excluding commands.
 *
 * \param[in] rgpm  Array of paint mask bytes.
 *
 * \returns         Count of paint mask bytes, might be zero.
 *
 * \note
 * Doesn't take account of possible mm padding, should be done by caller.
 * Assumes paint masks are bytes - need to change if otherwise.
 */
static int32 pm_findsize_exclude_commands(
                   /*@notnull@*/ /*@in@*/ const paint_mask_t rgpm[])
{
  int32 cb = 0;

  HQASSERT(rgpm != NULL, "NULL paint mask byte array address");

  if ( rgpm[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */
    int32 ipm = -1;

    do {
      ++ipm;
    } while ( PM_CHAIN_SET(rgpm[ipm]) && rgpm[ipm + 1] != PM_CMD );

    cb = (ipm + 1) * PM_SIZE;
  }

  return cb;

} /* Function pm_findsize_exclude_commands */


/** \brief Deduce number of COLORVALUEs following the paint mask.
 *
 * \param[in] rgpm   Pointer to array of paint mask bytes.
 *
 * \returns     Number of colorant bits set in paint mask bytes, or 0 if all
 *              current colorants (all0, all1)
 *
 * \note        All command codes have zero colorants - and we should never
 *              get /All sep at the start of the paint mask array.
 */
static int32 pm_total_colorants(
         /*@notnull@*/ /*@in@*/ const paint_mask_t rgpm[])
{
  int32           cci = 0;
  int32           ipm;
  paint_mask_t    pm;

  HQASSERT(rgpm != NULL, "NULL paint mask byte array address");

  if ( rgpm[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */
    ipm = -1;

    do {
      /* Move onto next paint mask */
      ++ipm;
      pm = PM_GET_MASK(rgpm[ipm]);

      /* Sum number of bits set */
      cci += count_bits_set_in_byte[pm];

    } while ( PM_CHAIN_SET(rgpm[ipm]) && rgpm[ipm + 1] != PM_CMD );

    HQASSERT(cci > 0, "paint mask has no colorant bits set");

    if ( PM_CHAIN_SET(rgpm[ipm]) &&
         rgpm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(rgpm[ipm + 2])) {
      /* /All sep present - bump colorant count by one for extra value */
      cci++;
      ipm += 2;
    }

    if ( PM_CHAIN_SET(rgpm[ipm]) &&
         rgpm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(rgpm[ipm + 2])) {
      /* Opacity is present - bump colorant count by one for extra value */
      cci++;
      ipm += 2;
    }

  }
#if defined( ASSERT_BUILD )
  else { /* Got special command code */

    HQASSERT(rgpm[1] == PM_CMD_ALL0 ||
             rgpm[1] == PM_CMD_ALL1 ||
             rgpm[1] == PM_CMD_NONE,
             "pm_total_colorants: got other than all0, all1 or none command code");
  }
#endif

  /* Return the count */
  return cci;

} /* Function pm_total_colorants */


/** \brief Return colorvalue offset for given colorant index.
 *
 * \param[in] rgpm          Array of paint masks.
 * \param ci                Index of colorant required.
 * \param[out] picv         Pointer to returned colorvalue offset.
 *
 * \retval TRUE   Colorant is present, or /All separation exists. The offset
 *                to the corresponding colorvalue from the start of the color
 *                entry's list is returned in \a picv.
 * \retval FALSE  Colorant is not present, and there is no /All separation.
 *                The colorvalue offset is not set.
 *
 * Explicit colorant indices of \c COLORANTINDEX_ALL or \c
 * COLORANTINDEX_ALPHA may be passed to this function to find the offset of
 * the All separation or opacity colorvalue.
 */
static Bool pm_colorant_offset(
         /*@notnull@*/ /*@in@*/ const paint_mask_t rgpm[],
                                COLORANTINDEX   ci,
        /*@notnull@*/ /*@out@*/ int32 *picv)
{
  HQASSERT(rgpm != NULL, "NULL pointer to paint mask array");
  HQASSERT(ci >= 0 || ci == COLORANTINDEX_ALL || ci == COLORANTINDEX_ALPHA,
           "invalid colorant index");
  HQASSERT(picv != NULL, "NULL pointer to returned colorant offset");

  if ( rgpm[0] != PM_CMD ) {
    /* Got normal pm array - start counting offset */
    int32 ccv = 0;
    /* Get pm index for colorant. Note that we cast the colorant index to
       uint32, which will make the special negative colorant indices have an
       ipmColorant higher than any possible colorant, and so scan to the end
       of the paint mask bytes. */
    int ipmColorant = PM_INDEX((uint32)ci);
    int ipm = 0 ;

    /* If ci is large enough (32 or more?), we could use vectorisable test to
       check if any of the bytes in a long word are zero, i.e., PM_CMD.
       Probably not worth the effort because of the large ci required, and
       the paint mask is likely to start 2-byte but not 4-byte aligned
       anyway. */
    do {
      paint_mask_t pm = PM_GET_MASK(rgpm[ipm]);

      HQASSERT(rgpm[ipm] != PM_CMD, "Shouldn't have CMD in paintmask bytes") ;

      if ( ipm == ipmColorant ) {
        /* Get colorant bit in the mask */
        int colorant_bit = PM_BIT_INDEX(ci);

        /* Check if colorant exists (and therefore offset valid) */
        if ( PM_COLORANT_SET(pm, colorant_bit) ) {
          /* Colorant is present - count colorants before it */
          ccv += count_bits_set_in_byte[pm & grgpmMaskLead[colorant_bit]];

          /* Return the final offset */
          *picv = ccv;
          return TRUE ;
        }
        /* else carry on to the end of the paintmask, so we can check for All */
      }

      if ( !PM_CHAIN_SET(rgpm[ipm]) ) /* No more paintmask bytes */
        return FALSE ;

      /* Keep track of total number of bits set. */
      ccv += count_bits_set_in_byte[pm];
    } while ( rgpm[++ipm] != PM_CMD ) ;

    /* PM byte for colorant was not found. If the command is an All
       separation, and we either wanted a real colorant or All, we can
       return its index. */
    if ( PM_IS_CMD_ALLSEP(rgpm[ipm + 1]) ) {
      if ( ci != COLORANTINDEX_ALPHA ) {
        HQASSERT(ci == COLORANTINDEX_ALL || ci >= 0,
                 "Returning All colorant index unexpectedly.") ;
        *picv = ccv;
        return TRUE;
      }

      /* If there are no more commands, we cannot find Alpha. */
      if ( !PM_CHAIN_SET(rgpm[ipm + 1]) )
        return FALSE ;

      ipm += 2 ;
      HQASSERT(rgpm[ipm] == PM_CMD, "Chain set but no following command") ;
      ccv += 1 ;
    }

    if ( ci == COLORANTINDEX_ALPHA &&
         PM_IS_CMD_OPACITY(rgpm[ipm + 1]) ) {
      *picv = ccv;
      return TRUE;
    }
  }

  return FALSE;
} /* Function pm_colorant_offset */


/** \brief Set up special paint mask command codes.
 *
 * \param[out] rgpm  Pointer to paint mask array to set up command in.
 * \param pmCmd      The special paint mask command to setup.
 *
 * \returns          Length of paint mask special command added in bytes.
 */
static int32 pm_setup_cmd(
  /*@notnull@*/ /*@out@*/ paint_mask_t rgpm[],
                          paint_mask_t pmCmd)
{
  HQASSERT(rgpm != NULL, "NULL pointer to paint mask array");

  /* Add escape code followed by special command */
  rgpm[0] = PM_CMD;
  rgpm[1] = pmCmd;

  /* For now special commands are of fixed size */
  return PM_SIZE_CMD;

} /* Function pm_setup_cmd */


/** \brief To completely setup a paint mask from an array of colorant indexes.
 *
 * \param[out] rgpm         Pointer to array of paintmasks to fill.
 * \param cci               Count of colorant indexes.
 * \param[in] rgci          Array of colorant indexes.
 * \param[out] pcciPad      Pointer to count of extra /All separation values,
 *                          inserted at the end of the colorvalues to round
 *                          out the final paintmask entry.
 * \param opacity           Opacity of color entry
 *
 * \returns                 Length of used paint mask in bytes.
 */
static int32 pm_setup(
/*@notnull@*/ /*@out@*/ paint_mask_t  rgpm[],
                        int32         cci,
/*@notnull@*/ /*@in@*/  const COLORANTINDEX rgci[],
/*@notnull@*/ /*@out@*/ int32*        pcciPad,
                        COLORVALUE    opacity)
{
  Bool    fAllSep;
  int32   ipm;
  int32   ipmLast;
  int32   ici;
  int32   bit_index;
  int32   cb;
  int32   cbCmd = 0;

  HQASSERT(rgpm != NULL, "NULL pointer to paint mask array");
  HQASSERT(cci > 0, "-ve or zero count of colorant indexes");
  HQASSERT(rgci != NULL, "NULL pointer to colorant index array");
  HQASSERT(pcciPad != NULL, "NULL pointer to returned number padded colorant indexes");
  HQASSERT(pm_in_order(cci, rgci), "colorant indexes are not in order");

  *pcciPad = 0;

  /* See if need to handle /All sep index */
  fAllSep = (rgci[0] == COLORANTINDEX_ALL);

  /* Find highest index paint mask */
  ipmLast = PM_INDEX(rgci[cci - 1]);

  HQASSERT(ipmLast + (fAllSep ? 3 : 1) <= DLC_BUFFER_SIZE, "paint mask will overflow buffer");

  /* Start with the first colorant index after any /All sep index */
  ici = fAllSep ? 1 : 0;

  HQASSERT(!fAllSep || cci > 1, "invalid count of colorant indexes when handling /All sep");

  /* Fill in all paint masks upto highest colorant index */
  ipm = 0;
  do {
    /* Clear out paint mask */
    rgpm[ipm] = 0;

    /* If not last paint mask set chain bit */
    if ( ipm < ipmLast ) {
      PM_SET_CHAIN(rgpm[ipm]);
    }

    /* Fill in paint bits for colorants in this paint mask */
    while ( ici < cci && PM_INDEX(rgci[ici]) == ipm ) {
      rgpm[ipm] |= grgpmMask[PM_BIT_INDEX(rgci[ici])];
      ici++;
    }

  } while ( ++ipm <= ipmLast );

  ipm = ipmLast;

  HQASSERT(ici == cci, "did not add all colorant indexes to paint mask");

  if ( fAllSep ) {
    /* Handle /All sep index - mark last normal paint mask that one follows */
    PM_SET_CHAIN(rgpm[ipm]);

    /* See if need to pad last paint mask for /All sep handling */
    bit_index = PM_BIT_INDEX(rgci[cci - 1]);
    if ( bit_index > 0 ) {
      rgpm[ipm] |= grgpmMaskTrail[bit_index];
      *pcciPad = count_bits_set_in_byte[grgpmMaskTrail[bit_index]];
    }

    /* Add all separation command code on end of paint mask */
    cbCmd = pm_setup_cmd(&rgpm[ipm + 1], PM_CMD_ALLSEP);
    ipm += 2;
  }

  if (opacity < COLORVALUE_ONE) {
    PM_SET_CHAIN(rgpm[ipm]);
    cbCmd += pm_setup_cmd(&rgpm[ipm + 1], PM_CMD_OPACITY);
    ipm += 2;
  }

  /* Calculate number of bytes used for paint mask */
  cb = (ipmLast + 1)*PM_SIZE + cbCmd;

  return cb;

} /* Function pm_setup */


/** \brief Copy paint mask into new paint mask array.
 *
 * \param[out] pm_dst      Pointer to destination paint mask array.
 * \param[in] pm_src       Pointer to source paint mask array.
 *
 * \returns     Length of paint mask in bytes.
 */
static int32 pm_copy(
/*@notnull@*/ /*@out@*/ paint_mask_t pm_dst[],
 /*@notnull@*/ /*@in@*/ const paint_mask_t pm_src[])
{
  int32 cb;
  int32 ipm = -1;

  HQASSERT(pm_dst != NULL, "NULL pointer to destination paint mask array");
  HQASSERT(pm_src != NULL, "NULL pointer to source paint mask array");

  if ( pm_src[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */

    do {
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
    } while ( PM_CHAIN_SET(pm_src[ipm]) && pm_src[ipm + 1] != PM_CMD );

    if ( PM_CHAIN_SET(pm_src[ipm]) &&
         pm_src[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(pm_src[ipm + 2])) {
      /* /All sep present */
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
    }

    if ( PM_CHAIN_SET(pm_src[ipm]) &&
         pm_src[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(pm_src[ipm + 2])) {
      /* Opacity is present */
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
    }

    if ( PM_CHAIN_SET(pm_src[ipm]) &&
         pm_src[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(pm_src[ipm + 2])) {
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
      ++ipm;
      pm_dst[ipm] = pm_src[ipm];
      /* Note: it is possible to have a valid paint mask byte of zero at the end
         here, and it is not a CMD. This is because colorants may have been added
         in the relevant byte but then removed afterwards. */
      do {
        ++ipm;
        pm_dst[ipm] = pm_src[ipm];
      } while ( PM_CHAIN_SET(pm_src[ipm]) /* && (pm_src[ipm + 1] != PM_CMD) */);
    }
  } else { /* Got special command code (all0, all1 or none - all the same size) */
    ++ipm;
    pm_dst[ipm] = pm_src[ipm];
    ++ipm;
    HQASSERT(PM_IS_VALID_CMD(pm_src[ipm]), "Invalid dl color cmd");
    pm_dst[ipm] = pm_src[ipm];
  }
  HQASSERT(!PM_CHAIN_SET(pm_src[ipm]), "A dl color paint mask terminated early");

  cb = (ipm + 1) * PM_SIZE;

  return cb;

} /* Function pm_copy */


/** \brief Check for exact equality of two paint masks, including any trailing
 * command and overprint information.
 *
 * \param[in] pm1     Pointer to first paint mask array.
 * \param[in] pm2     Pointer to second paint mask array.
 *
 * \retval TRUE if paint mask arrays are equal.
 * \retval FALSE if paint masks are not equal.
 *
 * \note Command code check after ordinary pm bytes is a bit loose, will
 * need changing for newer command codes.
 */
static Bool pm_equal(
 /*@notnull@*/ /*@in@*/ const paint_mask_t *pm1,
 /*@notnull@*/ /*@in@*/ const paint_mask_t *pm2)
{
  Bool equal;
  int32 ipm = -1;

  HQASSERT(pm1, "pm_equal: NULL pointer to first paint mask");
  HQASSERT(pm2, "pm_equal: NULL pointer to second paint mask");

  if ( pm1[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */
    do {
      ++ipm;
      equal = (pm1[ipm] == pm2[ipm]);
    } while ( equal && PM_CHAIN_SET(pm1[ipm]) && pm1[ipm + 1] != PM_CMD );

    if ( equal &&
         PM_CHAIN_SET(pm1[ipm]) &&
         pm1[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(pm1[ipm + 2])) {
      /* /All sep present */
      equal = (pm1[ipm + 1] == pm2[ipm + 1] && pm1[ipm + 2] == pm2[ipm + 2]);
      ipm += 2;
    }

    if ( equal &&
         PM_CHAIN_SET(pm1[ipm]) &&
         pm1[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(pm1[ipm + 2])) {
      /* Opacity is present */
      equal = (pm1[ipm + 1] == pm2[ipm + 1] && pm1[ipm + 2] == pm2[ipm + 2]);
      ipm += 2;
    }

    if ( equal &&
         PM_CHAIN_SET(pm1[ipm]) &&
         pm1[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(pm1[ipm + 2])) {
      equal = (pm1[ipm + 1] == pm2[ipm + 1] && pm1[ipm + 2] == pm2[ipm + 2]);
      ipm += 2;
      /* Note: it is possible to have a valid paint mask byte of zero at the end
         here, and it is not a CMD. This is because colorants may have been added
         in the relevant byte but then removed afterwards. */
      do {
        ++ipm;
        equal = equal && (pm1[ipm] == pm2[ipm]);
      } while ( equal && PM_CHAIN_SET(pm1[ipm]) /* && (pm1[ipm + 1] != PM_CMD) */);
    }

  } else { /* Got special command code (all0, all1 or none - all the same size) */
    equal = (pm1[0] == pm2[0] && pm1[1] == pm2[1]);
    ipm +=2;
    HQASSERT(PM_IS_VALID_CMD(pm1[1]), "Invalid dl color cmd");
  }
  HQASSERT(!equal || !PM_CHAIN_SET(pm1[ipm]),
           "A dl color paint mask terminated early");

  return equal;

} /* Function pm_equal */


/** \brief Utility procedure to scan the paint mask until it finds overprint
 * information (indicated by an escape opcode).
 *
 * \param[in] pm    The paint mask to search.
 * \param[out] pipm The index in the paint mask where the overprint bits start
 *                  (the byte beyond the opcode).
 *
 * If the paint mask does not contain any overprint information, \a pipm is
 * set to -1.
 */
static void pm_locate_overprints(
          /*@notnull@*/ /*@in@*/ const paint_mask_t *pm,
         /*@notnull@*/ /*@out@*/ int16 *pipm)
{
  HQASSERT(pm != NULL, "pm_locate_overprints: NULL pm");
  HQASSERT(pipm != NULL, "pm_locate_overprints: NULL pipm");

  /* Until proven otherwise, assume no overprint paint masks */
  *pipm = -1;

  if ( pm[0] != PM_CMD ) {
    /* Got ordinary paint mask array - loop over all chained paint mask bytes */
    int16 ipm = -1;

    do {
      ++ipm;
    } while ( PM_CHAIN_SET(pm[ipm]) && pm[ipm + 1] != PM_CMD );

    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(pm[ipm + 2])) {
      /* /All sep present */
      ipm += 2;
    }

    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(pm[ipm + 2])) {
      /* Opacity is present */
      ipm += 2;
    }

    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(pm[ipm + 2])) {
      /* Found overprint paint masks */
     *pipm = ipm + 3;
    }
  }
}

/* ------------- Private color entry functions --------------- */


/**
 * Macro to provide basic validation of a color entry wrapper struct
 * Tests in order are -
 * 1. actually point to something
 * 2. sensible reference count (not -ve as set by tell tale in debug)
 * 3. paint mask pointer points just after ref count pointer
 * 4. color values pointer is after paint mask pointer
 * 5. color values pointer is appropriately alligned.
 * 6. count of channels is sensible (not -ve)
 * 7. overprint counter is possible
 * 8. opacity is sensible
 * 9. All value is sensible
 */
#define CE_IS_VALID(p) ((p) != NULL &&                                  \
                        (p)->prc != NULL &&                             \
                        (p)->ppm == (paint_mask_t*)((p)->prc + 1) &&    \
                        (paint_mask_t*)(p)->pcv > (p)->ppm &&           \
                        (uintptr_t)(p)->pcv % sizeof(COLORVALUE) == 0 && \
                        (p)->ccv >= 0 &&                                \
                        (p)->iop >= -1 &&                               \
                        (p)->opacity <= COLORVALUE_ONE &&               \
                        ((p)->allsep == COLORVALUE_TRANSPARENT ||       \
                         (p)->allsep <= COLORVALUE_MAX))


/** \brief Build a color entry from a pointer to n color data.
 *
 * \param[out] pce       Pointer to color entry to build.
 * \param[in]  p_ncolor  Pointer to n color data.
 */
static void ce_from_ncolor(
   /*@notnull@*/ /*@out@*/ color_entry_t *pce,
    /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor)
{
  int32   cbpm;
  int32   ccv = 0, allindex = -1, alphaindex = -1 ;
  paint_mask_t *pm ;
  int16 ipm = -1 ;

  HQASSERT(pce != NULL, "NULL pointer to color entry to build");
  HQASSERT(p_ncolor != NULL, "NULL pointer to color data");

  /* Set up initial pointers */
  pce->prc = &p_ncolor->refs;
  pce->ppm = &p_ncolor->pm[0];
  pce->opacity = COLORVALUE_ONE ; /* Default, unless told otherwise */
  pce->allsep = COLORVALUE_TRANSPARENT ; /* Default, unless told otherwise */
  pce->iop = -1 ; /* Default, unless told otherwise */

  /* This function is called many times, so we'll loop over the paintmask
     directly, extracting all of the information that we need from it in one
     pass. */
  pm = pce->ppm ;
  if ( pm[0] != PM_CMD ) {
    do {
      ++ipm ;

      /* Sum number of bits set */
      ccv += count_bits_set_in_byte[PM_GET_MASK(pm[ipm])];

    } while ( PM_CHAIN_SET(pm[ipm]) && pm[ipm + 1] != PM_CMD ) ;

    /* ipm now points at the last byte of the paint mask. */
    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(pm[ipm + 2])) {
      /* /All sep present - bump colorant count by one for extra value */
      allindex = ccv++;
      ipm += 2;
    }

    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(pm[ipm + 2])) {
      /* Opacity is present - bump colorant count by one for extra value */
      alphaindex = ccv++;
      ipm += 2;
    }

    if ( PM_CHAIN_SET(pm[ipm]) &&
         pm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(pm[ipm + 2])) {
      ipm += 2;

      /* Set the overprint index to the first byte of the mask */
      pce->iop = ipm + 1 ;

      /* Note: it is possible to have a valid paint mask byte of zero at the end
         here, and it is not a CMD. This is because colorants may have been added
         in the relevant byte but then removed afterwards. */
      do {
        ++ipm;
      } while ( PM_CHAIN_SET(pm[ipm]) );
    }

    cbpm = (ipm + 1) * PM_SIZE;
  } else {
    cbpm = PM_SIZE_CMD ;
    if ( pm[1] == PM_CMD_ALL0 ) { /* Black */
      pce->allsep = 0 ;
    } else if ( pm[1] == PM_CMD_ALL1 ) { /* White */
      pce->allsep = COLORVALUE_ONE ;
    } else {
      HQASSERT(pm[1] == PM_CMD_NONE,
               "Got other than all0, all1 or none command code");
    }
    ipm += 2;
  }
  HQASSERT(!PM_CHAIN_SET(pm[ipm]), "A dl color paint mask terminated early");

  /* Set number of channels */
  pce->ccv = CAST_SIGNED_TO_INT16(ccv) ;

  /* Calc offset of color values after header data */
  pce->pcv = (COLORVALUE*)((uint8*)pce->prc + DLC_HEADERSIZE(cbpm));

  /* Unpack alpha and All channels, if they exist. */
  if ( alphaindex >= 0 ) {
    pce->opacity = pce->pcv[alphaindex];
    HQTRACE(pce->opacity >= COLORVALUE_ONE, ("Alpha 1.0 stored in DL color")) ;
  }

  if ( allindex >= 0 ) {
    pce->allsep = pce->pcv[allindex];
    HQASSERT(pce->allsep <= COLORVALUE_MAX,
             "Transparent All channel in DL color") ;
  }

  HQASSERT(CE_IS_VALID(pce), "invalid color entry built");

} /* Function ce_from_ncolor */


#define pm_from_ncolor( _p_ncolor ) (&(_p_ncolor)->pm[0])

#define pm_from_dlc( _pdlc1 ) \
  ((_pdlc1)->ce.ppm)

/** \brief Build a dl color.
 *
 * \param[out] pce       Pointer to color entry to fill in.
 * \param cci            Number of color indexes and values passed in.
 * \param[in] rgci       Pointer to array of colorant indexes.
 * \param[in] rgcv       Pointer to array of color values.
 */
static void ce_build(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ color_entry_t *pce,
                          int32 cci,
  /*@notnull@*/ /*@in@*/  const COLORANTINDEX rgci[],
  /*@notnull@*/ /*@in@*/  const COLORVALUE rgcv[])
{
  int32   cbpm;
  int32   cbHeader;
  int32   cciPad;
  int32   icv;
  int32   icvStart;
  int32   ccvCopy;
  Bool   fAllSep;

  HQASSERT(pce != NULL,
           "ce_build: NULL pointer to color entry to fill in");

  /* Set up new ref count */
  debug_scribble_buffer(context, cci);
  pce->prc = (ref_count_t*)&context->grgbColorBuffer[0];
  *(pce->prc) = (ref_count_t)1;

  /* Fill in paint mask */
  pce->ppm = (paint_mask_t*)(pce->prc + 1);
  cbpm = pm_setup(pce->ppm, cci, rgci, &cciPad, context->currentopacity);

  /* Setup pointer to color values and channel count */
  cbHeader = DLC_HEADERSIZE(cbpm);
  pce->pcv = (COLORVALUE*)((uint8*)pce->prc + cbHeader);
  pce->ccv = CAST_SIGNED_TO_INT16(cci + cciPad);
  if (context->currentopacity < COLORVALUE_ONE)
    ++pce->ccv;
  pce->iop = -1; /* no overprint info at this stage */
  pce->opacity = context->currentopacity;
  pce->allsep = COLORVALUE_TRANSPARENT;

  HQASSERT(CE_IS_VALID(pce),
           "ce_build: invalid built color entry");
  HQASSERT((uint8*)(pce->pcv + pce->ccv) <= &context->grgbColorBuffer[DLC_BUFFER_SIZE],
           "ce_build: paint mask will overflow buffer");

  /* Modify start and length of colorvalues to copy if /All sep handling */
  fAllSep = (rgci[0] == COLORANTINDEX_ALL);
  icvStart = fAllSep ? 1 : 0;
  ccvCopy = cci - icvStart;

  /* Copy color values into n color data area */
  HqMemCpy(pce->pcv, &rgcv[icvStart], ccvCopy*sizeof(COLORVALUE));

  if ( fAllSep ) {
    /* Need to add pad values when handling /All sep (plus one for the all sep entry
       itself, hence the less-than-or-equal test) */
    for ( icv = 0; icv <= cciPad; icv++ ) {
      pce->pcv[ccvCopy + icv] = rgcv[0];
    }
    pce->allsep = rgcv[0];
  }

  if (context->currentopacity < COLORVALUE_ONE) {
    HQASSERT(pce->ccv - 1 == ccvCopy + cciPad, "Expected opacity to be the final value");
    pce->pcv[ccvCopy + cciPad] = context->currentopacity;
  }

} /* Function ce_build */


/** \brief Build a dl color entry that is a single pm command code.
 *
 * \param[out] pce       Pointer to color entry to fill in.
 * \param nCmd           Special command to build color for.
 *
 * \note       Only special commands handled are ALL0, ALL1, and NONE, none
 *             of which have color values.
 */
static void ce_build_cmd(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ color_entry_t *pce,
                         int32 nCmd)
{
  int32         cbpm;
  int32         cbHeader;
  paint_mask_t  pmCmd;

  HQASSERT(pce != NULL, "NULL pointer to color entry to fill in");

  /* Set up new ref count */
  debug_scribble_buffer(context, 0);
  pce->prc = (ref_count_t*)&context->grgbColorBuffer[0];
  *(pce->prc) = (ref_count_t)1;

  /* Fill in paint mask */
  pce->ppm = (paint_mask_t*)(pce->prc + 1);
  pce->allsep = COLORVALUE_TRANSPARENT; /* Default until told otherwise */

  switch ( nCmd ) {
  default:
    HQFAIL("ce_build_cmd: unknown special command code to build");
    /* FALLTHROUGH */

  case DLC_WHITE:
    pmCmd = PM_CMD_ALL1;
    pce->allsep = COLORVALUE_ONE;
    break;

  case DLC_BLACK:
    pmCmd = PM_CMD_ALL0;
    pce->allsep = 0;
    break;

  case DLC_NONE:
    pmCmd = PM_CMD_NONE;
    break;
  }
  cbpm = pm_setup_cmd(pce->ppm, pmCmd);

  /* Setup color values pointer and channel count to sensible defaults */
  cbHeader = DLC_HEADERSIZE(cbpm);
  pce->pcv = (COLORVALUE*)((uint8*)pce->prc + cbHeader);
  pce->ccv = 0;
  pce->iop = -1;
  pce->opacity = COLORVALUE_ONE;

  HQASSERT(CE_IS_VALID(pce), "ce_build_cmd: invalid color entry built");

} /* Function ce_build_cmd */


/** \brief Build new color entry in buffer from template color entry.
 *
 * \param[out] pce         Pointer to color entry to fill in.
 * \param[in] pceTemplate  Pointer to template color entry.
 */
static void ce_build_template(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/      color_entry_t*    pce,
  /*@notnull@*/ /*@in@*/       const color_entry_t*    pceTemplate)
{
  int32 cbpm;
  int32 cbHeader;

  HQASSERT(pce != NULL, "NULL pointer to color entry to build");
  HQASSERT(pceTemplate != NULL, "NULL pointer to template color entry");

  /* Set up new ref count */
  debug_scribble_buffer(context, pceTemplate->ccv);
  pce->prc = (ref_count_t*)&context->grgbColorBuffer[0];
  *(pce->prc) = (ref_count_t)1;

  /* Fill in paint mask */
  pce->ppm = (paint_mask_t*)(pce->prc + 1);
  cbpm = pm_copy(pce->ppm, pceTemplate->ppm);

  /* Setup pointer to color values and channel count */
  cbHeader = DLC_HEADERSIZE(cbpm);
  pce->pcv = (COLORVALUE*)((uint8*)pce->prc + cbHeader);

  /* Num channels the same - by definition */
  pce->ccv = pceTemplate->ccv;
  pce->iop = pceTemplate->iop;
  pce->opacity = pceTemplate->opacity;
  pce->allsep = pceTemplate->allsep;

  HQASSERT(CE_IS_VALID(pce), "invalid built color entry");

} /* Function ce_build_template */


/** \brief Test dl color represented by two color entries for equality.
 *
 * \param[in] pce1         Pointer to color entry to test.
 * \param[in] pce2         Pointer to other color entry to test.
 *
 * \retval TRUE if the two color entries are equal
 * \retval FALSE if the two color entries are not equal
 *
 * \note Would memcmp() for the color value test be quicker?
 * Would it be worth the effort?
 */
static Bool ce_equal(
 /*@notnull@*/ /*@in@*/ const color_entry_t*  pce1,
 /*@notnull@*/ /*@in@*/ const color_entry_t*  pce2)
{
  int32   icv;
  Bool    fEqual;

  HQASSERT(pce1 != NULL, "NULL pointer to first color entry");
  HQASSERT(pce2 != NULL, "NULL pointer to second color entry");
  HQASSERT(CE_IS_VALID(pce1), "invalid first color entry");
  HQASSERT(CE_IS_VALID(pce2), "invalid second color entry");

  /* Quick test for same color entries or what they point at */
  fEqual = (pce1 == pce2) || (pce1->prc == pce2->prc);

  if ( !fEqual ) {
    /* Check for paint masks being equal - cheap test before costly test */
    fEqual = (pce1->ccv == pce2->ccv) && pm_equal(pce1->ppm, pce2->ppm);

    if ( fEqual ) {
      /* Try and match all colorant values */
      for ( icv = 0; fEqual && (icv < pce1->ccv); icv++ ) {
        fEqual = pce1->pcv[icv] == pce2->pcv[icv];
      }
    }
  }

  return fEqual;

} /* Function ce_equal */


/** \brief Merge a single color value according to a merge rule.
 *
 * \param cv1          First colorvalue to merge
 * \param cv2          Second colorvalue to merge
 * \param action       The method by which the colorvalues are merged.
 * \param[out] cv_dst  The output colorvalue.
*/
static void ce_merge_values(COLORVALUE cv1,
                            COLORVALUE cv2,
                            dlc_merge_action_t action,
    /*@notnull@*/ /*@out@*/ COLORVALUE* cv_dst)
{
  switch (action) {
  case COMMON_COLORANT_DISALLOW:
    HQFAIL("COMMON_COLORANT_DISALLOW should have been dealt with already");
    break;
  case COMMON_COLORANT_TAKEFROMFIRST:
    /* Take common color value from the first color. */
    cv_dst[0] = cv1;
    break;
  case COMMON_COLORANT_AVERAGE:
    /* Common color value is made by 'averaging' the two values
       according to the rules below. */
    if (cv1 == COLORVALUE_TRANSPARENT) {
      cv_dst[0] = cv2;
    } else if (cv2 == COLORVALUE_TRANSPARENT) {
      cv_dst[0] = cv1;
    } else {
      COLORVALUE cv_mean;
      int32 dist_from_half;

      /* Work out the mean average between the two colorant values. */
      cv_mean = (COLORVALUE)(((uint32)cv1 + (uint32)cv2 + 1u) >> 1);
      HQASSERT(cv_mean <= COLORVALUE_MAX, "color value out of range");

      /* Of the three colorant values (cv1, cv2, and the mean value) choose
         the value nearest COLORVALUE_HALF.  The intention is:
         1) If all the merged values are the same (solid, color, halftone)
            the merged value is the same.
         2) The merged value is within the range cv1, cv2.
         3) The merged value should quantise to a halftoned color value if
            either value requires a halftoned cell (we never have a merged
            value that quantises to solid or clear if the other value
            requires a halftone cell).
         4) If both values would quantise to solid or clear, the merged
            value quantises to solid or clear.
       */
      cv_dst[0] = cv_mean;
      dist_from_half = abs(cv_mean - COLORVALUE_HALF);

      if (abs(cv1 - COLORVALUE_HALF) < dist_from_half) {
        cv_dst[0] = cv1;
        dist_from_half = abs(cv1 - COLORVALUE_HALF);
      }
      if (abs(cv2 - COLORVALUE_HALF) < dist_from_half)
        cv_dst[0] = cv2;

      HQASSERT(cv_dst[0] <= COLORVALUE_MAX, "color value out of range");
    }
    break;
  case COMMON_COLORANT_MERGEOVERPRINTS:
    if (cv1 == COLORVALUE_TRANSPARENT && cv2 != COLORVALUE_TRANSPARENT) {
      cv_dst[0] = COLORVALUE_MAX;
    } else {
      cv_dst[0] = cv1;
    }
    break;
  default:
    HQFAIL("Unrecognised COMMON_COLORANT_ dl color merge action");
    break;
  }
}

#define DLC_COPY_COLORS(pm, pp_cv_dst, pp_cv_src, p_cci) \
MACRO_START \
  int32 _ncv_; \
  _ncv_ = count_bits_set_in_byte[pm]; \
  HQASSERT(*pp_cv_dst != NULL && *pp_cv_src != NULL && _ncv_ > 0, \
           "DLC_COPY_COLORS: invalid arguments"); \
  HqMemCpy((*pp_cv_dst), (*pp_cv_src), (_ncv_ * sizeof(COLORVALUE))); \
  *pp_cv_dst += _ncv_; \
  *pp_cv_src += _ncv_; \
  *p_cci     += _ncv_; \
MACRO_END

/** \brief Merge two color entries to create a third.
 *
 * \param[out] pceTarget     Pointer to color entry to for result of merge.
 * \param[in] pceSource1     Pointer to color entry of first color to be merged
 * \param[in] pceSource2     Pointer to color entry of second color to be merged
 * \param action             Determines the behaviour for colorants that are
 *                           present in both colors.
 *
 * \retval TRUE if successfully merged
 * \retval FALSE if merging failed
 *
 * \note This routine does not merge overprint masks for max-blts, it simply
 * copies any masks from pceSource1 into pceTarget.  Max-blt overprint
 * merging should be handled by \c dlc_combine_overprints() which requires
 * the colors to have the same set of colorants.
 */
static Bool ce_merge(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ color_entry_t *pceTarget,
  /*@notnull@*/ /*@in@*/  const color_entry_t *pceSource1,
  /*@notnull@*/ /*@in@*/  const color_entry_t *pceSource2,
   dlc_merge_action_t action)
{
  int32        cbpm, cbpm1, cbpm2, cbHeader, cci;
  paint_mask_t *ppmTarget, *ppmSource1, *ppmSource2;
  COLORVALUE   *pcvTarget, *pcvSource1, *pcvSource2;
  int32 ibSource1, ibSource2;
  Bool has_allsep1, has_allsep2;

  HQASSERT(pceTarget != NULL, "ce_merge: pceTarget null");
  HQASSERT(CE_IS_VALID(pceSource1), "ce_merge: pceSource1 invalid");
  HQASSERT(CE_IS_VALID(pceSource2), "ce_merge: pceSource2 invalid");

  /* Setup new ref count */
  debug_scribble_buffer(context, pceSource1->ccv + pceSource2->ccv);
  pceTarget->prc = (ref_count_t*)&context->grgbColorBuffer[0];
  *(pceTarget->prc) = (ref_count_t)1;

  /* Setup pointers to paintmask */
  ppmTarget  = pceTarget->ppm = (paint_mask_t*)(pceTarget->prc + 1);
  ppmSource1 = pceSource1->ppm;
  HQASSERT(ppmSource1[0] != PM_CMD, "ce_merge: first color uses special cmd");
  ppmSource2 = pceSource2->ppm;
  HQASSERT(ppmSource2[0] != PM_CMD, "ce_merge: second color uses special cmd");

  has_allsep1 = (pceSource1->allsep != COLORVALUE_TRANSPARENT) ;
  has_allsep2 = (pceSource2->allsep != COLORVALUE_TRANSPARENT) ;
  /* Check there is no overlap between the colorants */
  if (has_allsep1 && has_allsep2 && action == COMMON_COLORANT_DISALLOW)
    return error_handler(UNDEFINEDRESULT);

  /* Number of bytes in merged pm is the max of the two input pm plus... */
  cbpm1 = pm_findsize_exclude_commands(pceSource1->ppm);
  cbpm2 = pm_findsize_exclude_commands(pceSource2->ppm);
  cbpm  = (cbpm1 > cbpm2 ? cbpm1 : cbpm2);
  /* ...any all sep command plus... */
  if (has_allsep1 || has_allsep2)
    cbpm += PM_SIZE_CMD;
  /* ...any opacity command plus... */
  if (pceSource1->opacity < COLORVALUE_ONE || pceSource2->opacity < COLORVALUE_ONE)
    cbpm += PM_SIZE_CMD;
  /* ...any overprint info from the first color */
  if ( pceSource1->iop != -1 )
    cbpm += pm_findsize(pceSource1->ppm) - pceSource1->iop + PM_SIZE_CMD;

  /* Setup pointer to color values and channel count */
  cbHeader = DLC_HEADERSIZE(cbpm);
  pcvTarget  = pceTarget->pcv = (COLORVALUE*)(((uint8*)pceTarget->prc) + cbHeader);
  pcvSource1 = pceSource1->pcv;
  pcvSource2 = pceSource2->pcv;

  /* Check new merged color will fit inside the buffer */
  HQASSERT(((uint8*)(pceTarget->pcv + pceSource1->ccv + pceSource2->ccv) <=
            &context->grgbColorBuffer[DLC_BUFFER_SIZE]),
           "ce_merge: paint mask will overflow buffer");

  cci = 0;

  do {
    paint_mask_t pmSource1, pmSource2;

    pmSource1 = PM_GET_MASK(*(ppmSource1));
    pmSource2 = PM_GET_MASK(*(ppmSource2));

    /* Interleave the two paint masks and color values */
    *ppmTarget++ = (paint_mask_t)(*ppmSource1++ | *ppmSource2++);

    /* Check there is no overlap between the colorants */
    if ( ((pmSource1 & pmSource2) != 0) ) {
      if ( action == COMMON_COLORANT_DISALLOW )
        return error_handler(UNDEFINEDRESULT);
    }

    if ( pmSource2 == 0 && pmSource1 != 0) {
      /* Copy paint mask and color values from first color */
      DLC_COPY_COLORS(pmSource1, &pcvTarget, &pcvSource1, &cci);
    }
    else if ( pmSource1 == 0 && pmSource2 != 0 ) {
      /* Copy paint mask and color values from second color */
      DLC_COPY_COLORS(pmSource2, &pcvTarget, &pcvSource2, &cci);
    }
    else if ( pmSource1 != 0 && pmSource2 != 0) {

      ibSource1 = highest_bit_set_in_byte[pmSource1];
      ibSource2 = highest_bit_set_in_byte[pmSource2];

      do {
        paint_mask_t pmTemp;

        if ( ibSource1 > ibSource2 ) { /* Colorants come from first dl color */
          /* Get bits from first pm before second pm bits, and copy the colors */
          pmTemp = (paint_mask_t)(pmSource1 & grgpmMaskLead[ibSource2]);
          DLC_COPY_COLORS(pmTemp, &pcvTarget, &pcvSource1, &cci);
          /* Remove bits from first pm pointer and update first bit pos */
          pmSource1 &= (~pmTemp);
          ibSource1 = highest_bit_set_in_byte[pmSource1];
        }
        else if ( ibSource1 == ibSource2 ) {
          /* Colorants common to both colors, only one colorant at a time */
          ce_merge_values(pcvSource1[0], pcvSource2[0], action, pcvTarget);
          ++pcvSource1; ++pcvSource2; ++pcvTarget; ++cci;
          pmTemp = grgpmMask[ibSource1];
          pmSource1 &= (~pmTemp);
          pmSource2 &= (~pmTemp);
          ibSource1 = highest_bit_set_in_byte[pmSource1];
          ibSource2 = highest_bit_set_in_byte[pmSource2];
        }
        else { /* Colorants from second dl color */
          /* Get bits from second pm before first pm bits, and copy the colors */
          pmTemp = (paint_mask_t)(pmSource2 & grgpmMaskLead[ibSource1]);
          DLC_COPY_COLORS(pmTemp, &pcvTarget, &pcvSource2, &cci);
          /* Remove bits from second pm pointer and update first bit pos */
          pmSource2 &= (~pmTemp);
          ibSource2 = highest_bit_set_in_byte[pmSource2];
        }
      } while ( (pmSource1 != 0) && (pmSource2 != 0) );
      /* Copy the remaining color values in this pm element */
      if ( pmSource1 != 0 )
        DLC_COPY_COLORS(pmSource1, &pcvTarget, &pcvSource1, &cci);
      else if ( pmSource2 != 0 )
        DLC_COPY_COLORS(pmSource2, &pcvTarget, &pcvSource2, &cci);
    }

    /* Continue merging until one or both paintmasks finish */
  } while ( PM_CHAIN_SET((ppmSource1[-1])) && ppmSource1[0] != PM_CMD &&
            PM_CHAIN_SET((ppmSource2[-1])) && ppmSource2[0] != PM_CMD );

  /* Finish merging if one dl color paint mask was longer than the other */
  while ( (PM_CHAIN_SET((ppmSource1[-1]))) ) {
    paint_mask_t pmSource;
    /* Copy paint mask and color values from first color */
    if ( *ppmSource1 == PM_CMD )
      break; /* Got overprint information */
    pmSource = PM_GET_MASK(*(ppmSource1));
    *ppmTarget++ = *ppmSource1++;
    if ( pmSource != 0 )
      DLC_COPY_COLORS(pmSource, &pcvTarget, &pcvSource1, &cci);
  }
  while ( (PM_CHAIN_SET((ppmSource2[-1]))) ) {
    paint_mask_t pmSource;
    /* Copy paint mask and color values from second color */
    if ( *ppmSource2 == PM_CMD )
      break; /* Got overprint information */
    pmSource = PM_GET_MASK(*(ppmSource2));
    *ppmTarget++ = *ppmSource2++;
    if ( pmSource != 0 )
      DLC_COPY_COLORS(pmSource, &pcvTarget, &pcvSource2, &cci);
  }

  /* Clear the chain bit now; if there are commands to be added then the chain
     bit is set before each command (and there may be more than more command). */
  PM_CLEAR_CHAIN(ppmTarget[-1]);

  /* Now merge the all-sep command and color value. */
  if (has_allsep1 || has_allsep2) {
    COLORVALUE allcv1 = COLORVALUE_TRANSPARENT;
    COLORVALUE allcv2 = COLORVALUE_TRANSPARENT;
    if (has_allsep1) {
      allcv1 = *pcvSource1++;
      ppmSource1 += 2;
    }
    if (has_allsep2) {
      allcv2 = *pcvSource2++;
      ppmSource2 += 2;
    }
    PM_SET_CHAIN(ppmTarget[-1]);
    *ppmTarget++ = PM_CMD;
    *ppmTarget++ = PM_CMD_ALLSEP;
    ce_merge_values(allcv1, allcv2, action, pcvTarget);
    ++pcvTarget;
    ++cci;
  }

  /* Now merge the opacity command and color value. */
  if (pceSource1->opacity < COLORVALUE_ONE || pceSource2->opacity < COLORVALUE_ONE) {
    COLORVALUE opacity1 = COLORVALUE_ONE;
    COLORVALUE opacity2 = COLORVALUE_ONE;
    if (pceSource1->opacity < COLORVALUE_ONE) {
      opacity1 = *pcvSource1++;
      ppmSource1 += 2;
    }
    if (pceSource2->opacity < COLORVALUE_ONE) {
      opacity2 = *pcvSource2++;
      ppmSource2 += 2;
    }
    HQASSERT(opacity1 == pceSource1->opacity, "bad dl color - opacity value mismatch");
    HQASSERT(opacity2 == pceSource2->opacity, "bad dl color - opacity value mismatch");
    PM_SET_CHAIN(ppmTarget[-1]);
    *ppmTarget++ = PM_CMD;
    *ppmTarget++ = PM_CMD_OPACITY;
    ce_merge_values(opacity1, opacity2, action, pcvTarget);
    ++pcvTarget;
    ++cci;
  }

  /* Include any overprint info from the first color (NOTE this is not a full
     merge of overprint masks - use dlc_combine_overprints for that). */
  if ( pceSource1->iop != -1 ) {
    /* Copy the overprint information from source1 into target */
    HQASSERT(ppmSource1[0] == PM_CMD, "ce_merge: expected PM_CMD");
    HQASSERT(PM_IS_CMD_OPMAXBLT(ppmSource1[1]), "ce_merge: unrecognised cmd");
    PM_SET_CHAIN(ppmTarget[-1]);
    *ppmTarget++ = *ppmSource1++; /* Copy PM_CMD */
    *ppmTarget++ = *ppmSource1++; /* Copy PM_CMD_OPMAXBLT */
    do {
      *ppmTarget++ = *ppmSource1++; /* Copy max-blt paint masks */
    } while ( (PM_CHAIN_SET((ppmSource1[-1]))) );
  }

  pceTarget->ccv = CAST_SIGNED_TO_INT16(cci);
  pm_locate_overprints(pceTarget->ppm, & pceTarget->iop);

  if (pm_colorant_offset(pceTarget->ppm, COLORANTINDEX_ALPHA, &cci))
    pceTarget->opacity = pceTarget->pcv[cci];
  else
    pceTarget->opacity = COLORVALUE_ONE;

  if (pm_colorant_offset(pceTarget->ppm, COLORANTINDEX_ALL, &cci))
    pceTarget->allsep = pceTarget->pcv[cci];
  else
    pceTarget->allsep = COLORVALUE_TRANSPARENT;

  HQASSERT(CE_IS_VALID(pceTarget), "ce_merge: built invalid pceTarget");

  return TRUE;

} /* Function ce_merge */


/** \brief Sets up a new color entry containing the colorants from a color
 * entry, but excluding a particular indexed colorant.
 *
 * \param[out] pceTarget     Pointer to color entry for the result.
 * \param ci                 Index of colorant to be removed
 * \param[in] pceSource      Pointer to color entry of source of colorants.
 */
static void ce_remove_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ color_entry_t *pceTarget,
                         COLORANTINDEX ci,
  /*@notnull@*/ /*@in@*/ const color_entry_t *pceSource)
{
  int32        cbpm, cbHeader, cci, ib;
  paint_mask_t *ppmTarget, *ppmSource;
  paint_mask_t pmTarget, pmSource, pmRemove;
  COLORVALUE   *pcvTarget, *pcvSource;
  int32        ipm, ipmLast, opm, opmLast;

  HQASSERT(pceTarget != NULL, "ce_remove_colorant: pceTarget null");
  HQASSERT(ci >= 0, "ce_remove_colorant: ci < 0");
  HQASSERT(CE_IS_VALID(pceSource), "pceSource is invalid");

  cci = 0;

  /* Setup new ref count */
  debug_scribble_buffer(context, pceSource->ccv);
  pceTarget->prc = (ref_count_t*)&context->grgbColorBuffer[0];
  *(pceTarget->prc) = (ref_count_t)1;

  /* Setup pointers to paintmask */
  ppmTarget  = pceTarget->ppm = (paint_mask_t*)(pceTarget->prc + 1);
  ppmSource  = pceSource->ppm;
  HQASSERT(ppmSource[0] != PM_CMD &&
           pceSource->allsep == COLORVALUE_TRANSPARENT &&
           pceSource->opacity == COLORVALUE_ONE,
           "ce_remove_colorant: source uses special cmd");

  /* Find the last paint mask byte of the new color */
  cbpm = ((pceSource->iop == -1) ? pm_findsize(ppmSource) : pceSource->iop - 2);
  ipmLast = cbpm/PM_SIZE;
  opmLast = -1 ;
  if ( pceSource->iop != -1 ) {
    cbpm = pm_findsize(ppmSource) ;
    opmLast = cbpm/PM_SIZE;
  }

  /* See if we can reduce (or remove) the overprint info. */
  opm = ipmLast + 2 ;
  if ( opmLast > 0 ) {
    int32 tmp = PM_INDEX( ci ) ;
    if ( opm + tmp + 1 == opmLast ) {
      pmSource = PM_GET_MASK(ppmSource[opmLast-1]);
      pmRemove = (paint_mask_t)grgpmMask[PM_BIT_INDEX(ci)];
      /* Can remove last op pm if only ci is present */
      if ( (pmSource & ~pmRemove) == 0 ) {
        --opmLast;
        cbpm -= PM_SIZE;
      }
      /* Can also remove any blank op pms between ci and colorants in dl color */
      while ( opmLast > opm &&
              PM_GET_MASK(ppmSource[opmLast-1]) == 0 ) {
        --opmLast;
        cbpm -= PM_SIZE;
      }
      if ( opmLast == opm ) {
        opmLast = -1 ;
        cbpm -= 2 ;
      }
    }
  }

  if ( (PM_INDEX(ci)+1) == ipmLast ) {
    /* Colorant to be removed is in the last paintmask
       - may be possible to reduce size of new color's pm */
    pmSource = PM_GET_MASK(ppmSource[ipmLast-1]);
    pmRemove = (paint_mask_t)grgpmMask[PM_BIT_INDEX(ci)];
    /* Can remove last pm if only ci is present */
    if ( (pmSource & ~pmRemove) == 0 ) {
      --ipmLast;
      cbpm -= PM_SIZE;
    }
    /* Can also remove any blank pms between ci and colorants in dl color */
    while ( PM_GET_MASK(ppmSource[ipmLast-1]) == 0 ) {
      HQASSERT(ipmLast > 1, "removing colorant meant creating none dl color");
      --ipmLast;
      cbpm -= PM_SIZE;
    }
  }

  /* Calculate number of bytes used for paint mask */
  cbHeader = DLC_HEADERSIZE(cbpm);

  /* Setup pointers to color values */
  pcvTarget = pceTarget->pcv = (COLORVALUE*)(((uint8*)pceTarget->prc) + cbHeader);
  pcvSource = pceSource->pcv;

  /* Now workout the new color's paintmask and color values */
  for ( ipm = 0; ipm < ipmLast; ++ipm ) {

    pmRemove = (paint_mask_t)((PM_INDEX(ci) == ipm) ? grgpmMask[PM_BIT_INDEX(ci)] : 0);
    pmSource = PM_GET_MASK(*(ppmSource++));

    pmTarget = (paint_mask_t)(pmSource & ~pmRemove);
    *ppmTarget = pmTarget;
    if ( ipm < (ipmLast-1) )
      PM_SET_CHAIN(*ppmTarget++);

    if ( pmTarget != 0 ) {
      if ( pmRemove == 0 ) {
        /* Can just do a straight copy */
        DLC_COPY_COLORS(pmSource, &pcvTarget, &pcvSource, &cci);
      }
      else {
        paint_mask_t pmTemp;

        ib = highest_bit_set_in_byte[pmRemove];

        pmTemp = (paint_mask_t)(pmSource & grgpmMaskLead[ib]);
        if ( pmTemp != 0 ) {
          /* Copy colorants preceding colorant to be removed */
          DLC_COPY_COLORS(pmTemp, &pcvTarget, &pcvSource, &cci);
        }

        ++pcvSource; /* Skip over colorant to be removed */

        pmTemp = (paint_mask_t)(pmSource & grgpmMaskTrail[ib]);
        if ( pmTemp != 0 ) {
          /* Copy colorants succeeding colorant to be removed */
          DLC_COPY_COLORS(pmTemp, &pcvTarget, &pcvSource, &cci);
        }
      }
    }
    else if ( pmSource != 0 ) {
      /* Skip over the unselected colorant */
      HQASSERT(count_bits_set_in_byte[pmSource]==1, "ce_remove_colorant: more than one bit set");
      ++pcvSource;
    }
  }

  if ( opmLast > 0 ) {
    ppmSource = & pceSource->ppm[ opm ] ;
    PM_SET_CHAIN(*ppmTarget++);
    *ppmTarget++ = ppmSource[ -2 ] ;
    *ppmTarget++ = ppmSource[ -1 ] ;
    ipm = opm ;
    while ( ipm < opmLast ) {
      pmRemove = (paint_mask_t)((opm + PM_INDEX(ci) == ipm) ? grgpmMask[PM_BIT_INDEX(ci)] : 0);
      pmSource = PM_GET_MASK(*(ppmSource++));

      pmTarget = (paint_mask_t)(pmSource & ~pmRemove);
      *ppmTarget = pmTarget;
      if ( ipm < (opmLast-1) )
        PM_SET_CHAIN(*ppmTarget++);
      ipm++ ;
    }
  }

  pceTarget->ccv = CAST_SIGNED_TO_INT16(cci);
  pm_locate_overprints(pceTarget->ppm, & pceTarget->iop);
  pceTarget->opacity = pceSource->opacity;
  pceTarget->allsep = pceSource->allsep;
  HQASSERT(CE_IS_VALID(pceTarget),"ce_remove_colorant: build invalid pceTarget");

} /* Function ce_remove_colorant */


/** \brief Find total storage size for dl color.
 *
 * \param[in] pce        Pointer to color entry.
 *
 * \returns Number of bytes dl color uses rounded up to mem align size.
 *
 * \note Includes padding required for correct allocation by mm.
 */
static mm_size_t ce_findsize(
      /*@notnull@*/ /*@in@*/ const color_entry_t *pce)
{
  mm_size_t   cb;

  HQASSERT(pce != NULL, "NULL pointer to color entry");
  HQASSERT(CE_IS_VALID(pce), "invalid color entry");

  /* Size is difference between start pointer and pointer off end in bytes */
  cb = (mm_size_t) ((uint8*)(pce->pcv + pce->ccv) - (uint8*)(pce->prc));

  /* Pad for mm required alignement */
  cb = DLC_ALIGNSIZE(cb, DLC_MM_ALIGNSIZE);

  HQASSERT(cb > 0 && cb % DLC_MM_ALIGNSIZE == 0,
           "ce_findsize: invalid color entry size");

  return cb;

} /* Function ce_findsize */


/* ------------- DL color cache functions --------------- */

#define DLC_CONTEXT_NAME "DL Color Context"

/**
 * Create a dlc context and initialise the color cache.
 *
 * There is no equivalent dlc_context_destroy because all the dl_color.c
 * allocations come from the dl pool.  For partial paints the dlc context is
 * unchanged, otherwise the dl pools are destroyed and the dlc_context ref is
 * simply nulled.
 */
Bool dlc_context_create(DL_STATE *page)
{
  dlc_context_t *context;
  size_t i;

  HQASSERT(page->dlc_context == NULL, "Already have a dlc context");

  context = dl_alloc(page->dlpools, sizeof_dlc_context_t(),
                     MM_ALLOC_CLASS_NCOLOR);
  if ( context == NULL )
    return error_handler(VMERROR);

  /* Make sub-arrays actually point to space beyond the end of context */
  {
    void *ptr = &context[1];
    context->grgdlsCache = ptr;
    ptr = ((char *)ptr) + dlc_cache_size()*DLC_ALIGN_UP(sizeof(dll_list_t));
    context->grgdce = ptr;
    ptr = ((char *)ptr) + 2*dlc_cache_size()*
                          DLC_ALIGN_UP(sizeof(dlc_cache_entry_t));
    context->grgdme = ptr;
  }

  context->grgbColorBuffer = dl_alloc(page->dlpools,
                                      DLC_BUFFER_SIZE * sizeof(uint8),
                                      MM_ALLOC_CLASS_NCOLOR);
  if ( context->grgbColorBuffer == NULL ) {
    dl_free(page->dlpools, context, sizeof_dlc_context_t(),
            MM_ALLOC_CLASS_NCOLOR);
    return error_handler(VMERROR);
  }

  /* Add cache entries to free list */
  for ( i = 0; i < 2*dlc_cache_size(); ++i ) {
    /* Cross link cache and MRU list entries */
    context->grgdme[i].pdce = &context->grgdce[i];
    context->grgdce[i].pdme = &context->grgdme[i];
  }

  /* Intialise free and MRU lists */
  DLL_RESET_LIST(&context->gdlsMRURef);
  DLL_RESET_LIST(&context->gdlsMRUUnRef);
  DLL_RESET_LIST(&context->gdlsFree);

  /* Initialise cache entry links and fill free list */
  for ( i = 0; i < 2*dlc_cache_size(); ++i ) {
    DLL_RESET_LINK(&context->grgdce[i], dll);

    DLL_RESET_LINK(&context->grgdme[i], dll);
    DLL_ADD_HEAD(&context->gdlsFree, &context->grgdme[i], dll);
  }

  /* Reset cache array lists */
  for ( i = 0; i < dlc_cache_size(); ++i ) {
    DLL_RESET_LIST(&context->grgdlsCache[i]);
  }

  /* Reset saved dl color memory cache values */
  context->gcbSave = 0;
  context->gp_ncolorSave = NULL;

#ifdef DLC_INSTRUMENT
  /* Reset instrumentation */
  HqMemZero(&context->gdim, sizeof(dlc_instrument_mem_t));
  HqMemZero(&context->gdir, sizeof(dlc_instrument_refs_t));
  HqMemZero(&context->gdic, sizeof(dlc_instrument_cache_t));
#endif /* DLC_INSTRUMENT */

  context->pools = page->dlpools;

  dlc_clear(&context->dlc_white);
  dlc_clear(&context->dlc_black);
  dlc_clear(&context->dlc_none);

  dlc_clear(&context->dlc_currentcolor);
  context->currentopacity = COLORVALUE_ONE;
  context->currentspflags = RENDER_KNOCKOUT;
  context->currentblacktype = BLACK_TYPE_UNKNOWN;

  NAME_OBJECT(context, DLC_CONTEXT_NAME);

  /* Create dlc_black, dlc_white and dlc_none colors when the cache is ready. */
  if ( !dlc_alloc_cmd(context, DLC_WHITE, &context->dlc_white) ||
       !dlc_alloc_cmd(context, DLC_BLACK, &context->dlc_black) ||
       !dlc_alloc_cmd(context, DLC_NONE, &context->dlc_none) ) {
    dl_free(page->dlpools, context->grgbColorBuffer,
            DLC_BUFFER_SIZE * sizeof(uint8),
            MM_ALLOC_CLASS_NCOLOR);
    dl_free(page->dlpools, context, sizeof_dlc_context_t(),
            MM_ALLOC_CLASS_NCOLOR);
    return FALSE;
  }

  page->dlc_context = context;
  return TRUE;
}


/** \brief To purge the dl color cache removing all unreferenced dl colors.
 *
 * \note        Basically move all entries in the unreferenced list onto the
 *              free list, freeing the underlying dl_color as we go.
 *
 *              We do this, after a partial paint (with vignette detection on)
 *              since these dl colors will be well distributed among the dl
 *              pool which is bad (ie catastrophic) for fragmentation.
 *              If/when we implement dl color allocation using mm allocation
 *              points (or, more likely, segregated allocation caches) then
 *              this can be removed since fragmentation will no longer be an
 *              issue.
 */
void dcc_purge(DL_STATE *page)
{
  dlc_context_t *context = page->dlc_context;
  dlc_mru_entry_t *pdme ;
  mm_size_t cb ;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);

  if ( !DLL_LIST_IS_EMPTY( &context->gdlsMRUUnRef )) {
    pdme = DLL_GET_HEAD( &context->gdlsMRUUnRef, dlc_mru_entry_t, dll ) ;
    while ( pdme != NULL ) {
#ifdef DLC_INSTRUMENT
      /* Count cache removals */
      context->gdic.cRemove++;
#endif /* DLC_INSTRUMENT */

      /* Free memory for old dl color */
      cb = ce_findsize( &( pdme->pdce->ce )) ;
      HQASSERT(*( pdme->pdce->ce.prc ) == ( ref_count_t )0 ,
               "dcc_purge: unreferenced dl color with ref count not 0" ) ;

      ncolor_free(context, cb, (p_ncolor_t *)(char *)&pdme->pdce->ce.prc) ;

      /* Remove cache entry from its cache entry list */
      DLL_REMOVE( pdme->pdce, dll ) ;
      pdme = DLL_GET_NEXT(pdme, dlc_mru_entry_t, dll) ;
    }

    /* Move all colors in unrefd list to free list */
    DLL_LIST_APPEND( &context->gdlsFree, &context->gdlsMRUUnRef ) ;

    HQASSERT(DLL_LIST_IS_EMPTY( &context->gdlsMRUUnRef ),
             "dcc_reset: referenced dl color list not emptied" ) ;
  }

#if defined( ASSERT_BUILD )
  /* Walk the cache checking that it contains no unreferenced dl colors */
  {
    uint32 uHash ;
    dlc_cache_entry_t *pdce ;

    for ( uHash = 0 ; uHash < dlc_cache_size(); uHash++ ) {
      pdce = DLL_GET_HEAD( &context->grgdlsCache[ uHash ], dlc_cache_entry_t, dll ) ;
      while ( pdce != NULL ) {
        HQASSERT(*( pdce->ce.prc ) > ( ref_count_t )0 ,
                 "dcc_purge: dl color in cache with 0 references" ) ;
        pdce = DLL_GET_NEXT(pdce, dlc_cache_entry_t, dll ) ;
      }
    }
  }
#endif
} /* Function dcc_purge */


/** \brief Compute hash value for dl color based on indexes and values.
 *
 * \param[in] pce       Pointer to color entry for dl color.
 *
 * \returns             Computed hash value in the range [0,dlc_cache_size()).
 *
 * \note Lots of fun to be had based on typical values for indexes
 * and color values.
 */
static uint32 dcc_hash(
 /*@notnull@*/ /*@in@*/ const color_entry_t *pce)
{
  uint32  uHash = 0;
  int32   icv;
  int32   ipm;

  HQASSERT(pce != NULL, "NULL pointer to dl color");
  HQASSERT(CE_IS_VALID(pce), "invalid color entry");

  /* Simple cmds have a hash of 0 */
  if ( pce->ppm[0] != PM_CMD ) {
    /* Hash colorvalues */
    for ( icv = 0; icv < pce->ccv; icv++ ) {
      uHash = (uHash << 4) + pce->pcv[icv];
    }

    /* Hash in paint masks */
    ipm = -1;
    do {
      ++ipm;
      uHash = (uHash << 4) + PM_GET_MASK(pce->ppm[ipm]);
    } while ( PM_CHAIN_SET(pce->ppm[ipm]) && pce->ppm[ipm + 1] != PM_CMD );

    if ( PM_CHAIN_SET(pce->ppm[ipm]) &&
         pce->ppm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_ALLSEP(pce->ppm[ipm + 2])) {
      /* All sep present */
      ipm += 2;
      uHash += pce->ppm[ipm];
    }

    if ( PM_CHAIN_SET(pce->ppm[ipm]) &&
         pce->ppm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPACITY(pce->ppm[ipm + 2])) {
      /* Opacity is present */
      ipm += 2;
      uHash += pce->ppm[ipm];
    }

    if ( PM_CHAIN_SET(pce->ppm[ipm]) &&
         pce->ppm[ipm + 1] == PM_CMD &&
         PM_IS_CMD_OPMAXBLT(pce->ppm[ipm + 2])) {
      ipm += 2;
      uHash += pce->ppm[ipm];
      /* Note: it is possible to have a valid paint mask byte of zero at the end
         here, and it is not a CMD. This is because colorants may have been added
         in the relevant byte but then removed afterwards. */
      do {
        ++ipm;
        uHash = (uHash << 4) + PM_GET_MASK(pce->ppm[ipm]);
      } while ( PM_CHAIN_SET(pce->ppm[ipm]) /* && (pce->ppm[ipm + 1] != PM_CMD) */);
    }
    HQASSERT(!PM_CHAIN_SET(pce->ppm[ipm]), "A dl color paint mask terminated early");
  }

  uHash %= dlc_cache_size();

  HQASSERT(dlc_hash_valid(uHash), "dcc_hash: invalid hash calculated");

  return uHash;

} /* Function dcc_hash */


/** \brief Find matching cache entry that can have another reference added.
 *
 * \param uHash     Hash code for color entry.
 * \param[in] pce   Pointer to color entry for dl color
 *
 * \returns First matching cache entry which can take another reference,
 *          else NULL
 */
static dlc_cache_entry_t* dcc_find(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
   uint32            uHash,    /* I */
  /*@notnull@*/ /*@in@*/ const color_entry_t *pce)
{
  dlc_cache_entry_t*  pdce;
  dlc_cache_entry_t*  pdceMatch = NULL;

  HQASSERT(pce != NULL, "NULL pointer to dl color");
  HQASSERT(dlc_hash_valid(uHash), "invalid hash code");
  HQASSERT(uHash == dcc_hash(pce), "hash code does not match");

  /* See if there are any cache entries based on the hash code */
  pdce = DLL_GET_HEAD(&context->grgdlsCache[uHash], dlc_cache_entry_t, dll);

  /* We have one or more potential matches - search list for match */
  while ( (pdce != NULL) && (pdceMatch == NULL) ) {

    if ( (*(pdce->ce.prc) < DLC_MAX_REFCOUNT) && ce_equal(&(pdce->ce), pce) ) {
      /* Found usable matching color entry - return it */
      pdceMatch = pdce;

#ifdef DLC_INSTRUMENT
      /* Count cache hits */
      context->gdic.cHit++;
#endif /* DLC_INSTRUMENT */

    } else { /* Match failed - move onto next cache entry */
      pdce = DLL_GET_NEXT(pdce, dlc_cache_entry_t, dll);
    }
  }

  HQASSERT(pdceMatch == NULL || *(pdceMatch->ce.prc) < DLC_MAX_REFCOUNT,
           "dcc_find: cache entry ref count maxed out");

  return pdceMatch;

} /* Function dcc_find */


/** \brief To add a color entry to the dl color cache.
 *
 * \param uHash       Hash code for color entry
 * \param[in] pce     Pointer to color entry to add to cache
 *
 * \note        Only brand new dl_colors should be added to the cache
 *              that is the reference count should be 1 when this
 *              function is called.
 */
static void dcc_add(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
   uint32          uHash,    /* I */
  /*@notnull@*/ /*@in@*/ const color_entry_t *pce)
{
  mm_size_t           cb;
  dlc_mru_entry_t*    pdme;

  HQASSERT(pce != NULL, "NULL pointer to color entry");
  HQASSERT(dlc_hash_valid(uHash), "invalid hash code");
  HQASSERT(uHash == dcc_hash(pce), "inconsistent hash code");
  HQASSERT(*(pce->prc) == (ref_count_t)1,
           "adding color to cache with multiple references");

#ifdef DLC_INSTRUMENT
  /* Count cache additions */
  context->gdic.cAdd++;
#endif /* DLC_INSTRUMENT */

  if ( !DLL_LIST_IS_EMPTY(&context->gdlsFree) ) {
    /* Pick up free cache entry */
    pdme = DLL_GET_TAIL(&context->gdlsFree, dlc_mru_entry_t, dll);

  } else { /* Have to recycle an existing used cache entry */

    if ( !DLL_LIST_IS_EMPTY(&context->gdlsMRUUnRef) ) {
      /* Got some unreferenced dl colors - use them first */
      pdme = DLL_GET_TAIL(&context->gdlsMRUUnRef, dlc_mru_entry_t, dll);

      /* Finally free memory for old dl color */
      cb = ce_findsize(&(pdme->pdce->ce));
      ncolor_free(context, cb, (p_ncolor_t *)(char *)&pdme->pdce->ce.prc);

    } else { /* Reuse cache entry for dl color still referenced */
      pdme = DLL_GET_TAIL(&context->gdlsMRURef, dlc_mru_entry_t, dll);
    }

    /* Remove cache entry from its cache entry list */
    DLL_REMOVE(pdme->pdce, dll);
  }

  /* Remove from containing list and add to start of MRU referenced list */
  DLL_REMOVE(pdme, dll);
  DLL_ADD_HEAD(&context->gdlsMRURef, pdme, dll);

  /* Copy in color entry details */
  pdme->pdce->ce = *pce;

  /* Add cache entry at head of cache list */
  DLL_ADD_HEAD(&context->grgdlsCache[uHash], pdme->pdce, dll);

} /* Function dcc_add */


/** \brief To remove a color entry from the dl color cache.
 *
 * \param pce   Pointer to color entry to remove from cache.
 *
 * \note        Color entry is matched on the pointer to n color data, NOT
 *              on the n color data.
 *              It is possible for a color entry to disappear from the cache
 *              in which case we just free it.
 *              The assumption here is that dcc_remove is called for dl_colors
 *              whose ref count has reached 0.
 */
static void dcc_remove(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
   color_entry_t*  pce)      /* I */
{
  uint32              uHash;
  dlc_cache_entry_t*  pdce;

  HQASSERT(pce != NULL, "NULL pointer to color entry");
  HQASSERT(*(pce->prc) == (ref_count_t)0, "reference count not 0");

#ifdef DLC_INSTRUMENT
  /* Count cache removals */
  context->gdic.cRemove++;
#endif /* DLC_INSTRUMENT */

  /* Get start of cache entry list */
  uHash = dcc_hash(pce);
  pdce = DLL_GET_HEAD(&context->grgdlsCache[uHash], dlc_cache_entry_t, dll);

  /* Search list for matching n color data pointer */
  while ( (pdce != NULL) && (pdce->ce.prc != pce->prc) ) {
    pdce = DLL_GET_NEXT(pdce, dlc_cache_entry_t, dll);
  }

  if ( pdce != NULL ) {
    /* Color had cache entry - move to head of unreferenced list */
    DLL_REMOVE(pdce->pdme, dll);
    DLL_ADD_HEAD(&context->gdlsMRUUnRef, pdce->pdme, dll);
  }
  else if ( !DLL_LIST_IS_EMPTY(&context->gdlsFree) ) {
    /* It's not in the cache but since we've got spare entries on the
     * free list, we'll put it back into the cache (on the unreferenced
     * list) just in case.
     * Of course, this case is very rare but could in theory happen
     * given the right ordering of coc purges, dl color usage and partial
     * paints.
     * This cache entry will be the first to be re-used, and is purged
     * by dcc_purge when necessary.
     */
    dlc_mru_entry_t *pdme;
    pdme = DLL_GET_TAIL(&context->gdlsFree, dlc_mru_entry_t, dll);
    DLL_REMOVE(pdme, dll);
    DLL_ADD_HEAD(&context->gdlsMRUUnRef, pdme, dll);
    pdme->pdce->ce = *pce;
    DLL_ADD_TAIL(&context->grgdlsCache[uHash], pdme->pdce, dll);
  }
  else {
    mm_size_t   cb;
    /* No room on the free list, so just free it */
    cb = ce_findsize(pce);
    ncolor_free(context, cb, (p_ncolor_t *)(char *)&pce->prc);
#ifdef DLC_INSTRUMENT
    /* Count colors lost from cache */
    context->gdic.cLost++;
#endif /* DLC_INSTRUMENT */
  }

} /* Function dcc_remove */


/** \brief Rehash a color in the hash table, since its contents have changed.
 *
 * \param pce         Pointer to color entry to remove from cache
 * \param uOldHash    The hash value prior to the change
 */
static void dcc_rehash(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@in@*/ const color_entry_t*  pce,
   uint32          uOldHash) /* I */
{
  uint32              uHash;
  dlc_cache_entry_t*  pdce;

  HQASSERT(pce != NULL, "NULL pointer to color entry");

#ifdef DLC_INSTRUMENT
  /* Count cache removals */
  context->gdic.cRemove++;
#endif /* DLC_INSTRUMENT */

  pdce = DLL_GET_HEAD(&context->grgdlsCache[uOldHash], dlc_cache_entry_t, dll);
  /* Search list for matching n color data pointer */
  while ( pdce != NULL && pdce->ce.prc != pce->prc ) {
    pdce = DLL_GET_NEXT(pdce, dlc_cache_entry_t, dll);
  }

  HQASSERT(pdce != NULL, "entry not found in cache");

  if ( pdce != NULL ) {
    DLL_REMOVE(pdce, dll);

    /* Copy in color entry details */
    pdce->ce = *pce;

    /* Add cache entry at head of cache list */
    uHash = dcc_hash(pce);
    DLL_ADD_HEAD(&context->grgdlsCache[uHash], pdce, dll);

    /* Move to start of MRU list */
    DLL_REMOVE(pdce->pdme, dll);
    DLL_ADD_HEAD(&context->gdlsMRURef, pdce->pdme, dll);
  }

#ifdef DLC_INSTRUMENT
  else { /* Count colors lost from cache */
    context->gdic.cLost++;
  }
#endif /* DLC_INSTRUMENT */

} /* Function dcc_rehash */


/** \brief Update the dl color cache with the given dl color entry.
 *
 * \param pce       Pointer to color entry in buffer.
 *
 * \retval TRUE if cache updated ok
 * \retval FALSE if cache update failed (and sets VMERROR).
 *
 * \note        Since new dl colors are built in a permanent buffer we
 *              can just forget it when we find a match in the cache.
 */
static Bool dcc_update(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ color_entry_t *pce)
{
  mm_size_t           cb;
  uint32              uHash;
  dlc_cache_entry_t*  pdce;

  HQASSERT(pce != NULL, "NULL pointer to dl color entry");

  /* Look for matching useable cache entry */
  uHash = dcc_hash(pce);
  pdce = dcc_find(context, uHash, pce);

  if ( pdce != NULL ) {
    /* Color already exists in cache - use it instead */

    if ( *(pdce->ce.prc) == (ref_count_t)0 ) {
      /* Found unreferenced version - move from unreferenced to referenced list */
      DLL_REMOVE(pdce->pdme, dll);
      DLL_ADD_HEAD(&context->gdlsMRURef, pdce->pdme, dll);

#ifdef DLC_INSTRUMENT
      /* Count unreferenced cache colors rereferenced */
      context->gdic.cFound++;
#endif /* DLC_INSTRUMENT */
    }

    /* Return color entry and bump reference count */
    *pce = pdce->ce;
    ++(*(pce->prc));

    return TRUE ;

  } else { /* Color not yet in cache - make copy and add to cache */
    p_ncolor_t p_ncolor;

    cb = ce_findsize(pce);

    if ( ncolor_alloc(context, cb, &p_ncolor) ) {
      /* Got memory to copy n color data to */
      HqMemCpy(p_ncolor, pce->prc, cb);

      /* Rebuild color entry for n color data, set ref count back to 1 as new dl color */
      ce_from_ncolor(pce, p_ncolor);
      *(pce->prc) = (ref_count_t)1;
      dcc_add(context, uHash, pce);

      return TRUE;
    }
    else
      return FALSE;
  }

  /* Not reached. */

} /* Function dcc_update */


/* ------------- Private dl color functions --------------- */


/** \brief Register a new reference to a color entry.
 *
 * \param[in,out] pdlc Pointer to dl color entry getting the reference.
 *
 * \retval TRUE if got reference ok
 * \retval FALSE if reference count exceeded.
 *
 * \note        Implements copy on maxing ref count.
 *              Original dl_color should only change if a new copy in the
 *              cache is used.
 */
static Bool dlc_get_reference(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t*   pdlc)
{
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdlc->ce.prc != NULL, "NULL pointer to dl color entry data");
  HQASSERT(CE_IS_VALID(&(pdlc->ce)), "invalid color entry");

#ifdef DLC_INSTRUMENT
  /* Count number of times an existing color is referenced */
  context->gdir.cGetReference++;
#endif /* DLC_INSTRUMENT */

  if ( *(pdlc->ce.prc) < DLC_MAX_REFCOUNT ) {
    /* Ref count not maxed out - hang another ref off color */
    ++(*(pdlc->ce.prc));

#ifdef DLC_INSTRUMENT
    /* Count number of times an existing color is reused */
    context->gdir.cReUse++;
    if ( *(pdlc->ce.prc) > context->gdir.rcMax ) {
      context->gdir.rcMax = *(pdlc->ce.prc);
    }
#endif /* DLC_INSTRUMENT */

    /* Always succeeds - which is nice */
    return TRUE;

  } else { /* Ref count is maxed out - get cache to find equivalent or make copy */

#ifdef DLC_INSTRUMENT
    /* Count number of times an existing color has copy made */
    context->gdir.cCopyOnMax++;
#endif /* DLC_INSTRUMENT */

    /* Success depends on being able to use the cache */
    return dcc_update(context, &pdlc->ce);
  }

  /* NEVER REACHED */

} /* Function dlc_get_reference */


/** \brief Register a new reference to color data.
 *
 * \param[in,out] pp_ncolor       Pointer to color data
 *
 * \retval TRUE if got reference ok
 * \retval FALSE if the reference count is exceeded.
 *
 * \note        Implements copy on maxing ref count.
 *              Original dl_color should only change if a new copy in the
 *              cache is used.
 */
static Bool dl_get_reference(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t*   pp_ncolor)
{
  ref_count_t*    prc;
  color_entry_t   ce;

  HQASSERT(pp_ncolor != NULL, "NULL pointer to color data pointer");
  HQASSERT(*pp_ncolor != NULL, "NULL pointer to color data");

#ifdef DLC_INSTRUMENT
  /* Count number of times an existing color is referenced */
  context->gdir.cGetReference++;
#endif /* DLC_INSTRUMENT */

  prc = &(*pp_ncolor)->refs;

  if ( *prc < DLC_MAX_REFCOUNT ) {
    /* Ref count not maxed out - hang another ref off color */
    ++(*prc);

#ifdef DLC_INSTRUMENT
    /* Count number of times an existing color is reused */
    context->gdir.cReUse++;
    if ( *prc > context->gdir.rcMax ) {
      context->gdir.rcMax = *prc;
    }
#endif /* DLC_INSTRUMENT */

    /* Always succeeds - which is nice */
    return TRUE;

  } else { /* Maxed out ref count - look for cache entry that has refs spare */

#ifdef DLC_INSTRUMENT
    /* Count number of times an existing color has copy made */
    context->gdir.cCopyOnMax++;
#endif /* DLC_INSTRUMENT */

    /* Build color entry and get cache to find equivalent or make copy */
    ce_from_ncolor(&ce, *pp_ncolor);

    if ( dcc_update(context, &ce) ) {
      /* Got new ref from cache - return it */
      *pp_ncolor = (p_ncolor_t)ce.prc;
      return TRUE;
    }

    /* Failed to get new reference - leave original as is */
    return FALSE;
  }

  /* NEVER REACHED */

} /* Function dl_get_reference */


/* ------------- Public dl color functions --------------- */


/** \brief Free off color entry from listobject.
 *
 * \param[in] context   DL color context.
 * \param pp_ncolor     Pointer to n color data.
 */
void dl_release_(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor)
{
  color_entry_t ce;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT((*pp_ncolor)->refs > (ref_count_t)0, "reference count already 0");

  if ( !DL_REFS_CONSTANT(context, *pp_ncolor) ) {
    /* Not trying to release black or white constants */

    if ( --((*pp_ncolor)->refs) == (ref_count_t)0 ) {
      /* No more references to color data - build color entry for cache work */
      ce_from_ncolor(&ce, *pp_ncolor);

      /* Remove dl color from dl color cache */
      dcc_remove(context, &ce);
    }
  }

  /* Clear original reference */
  *pp_ncolor = NULL;

} /* Function dl_release_ */


/** \brief Free off color entry.
 *
 * \param pdlc        Pointer to dl color being released.
 */
void dlc_release_(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t *pdlc)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(*(pdlc->ce.prc) > (ref_count_t)0, "reference count already 0");

  if ( !DLC_REFS_CONSTANT(context, pdlc) ) {
    /* Not trying to release black or white constants */

    if ( --(*(pdlc->ce.prc)) == (ref_count_t)0 ) {
      /* No more references to dl color entry - remove from dl color cache */
      dcc_remove(context, &pdlc->ce);
    }
  }

  /* Clear out wrapper structure */
  dlc_clear(pdlc);

} /* Function dlc_release_ */


/** \brief Allocate special command only dl colors.
 *
 * \param nCmd       Type of special command to create dl color for.
 * \param[out] pdlc  Pointer to dl color entry to fillin with command color.
 *
 * \retval TRUE if created dl color ok
 * \retval FALSE if failed to create DL color (signals VMERROR).
 */
static Bool dlc_alloc_cmd(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         nCmd,         /* I */
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc)
{
  mm_size_t   cb;
  p_ncolor_t  p_ncolor;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry to fillin");

  /* Build color and its entry for command */
  ce_build_cmd(context, &pdlc->ce, nCmd);
  cb = ce_findsize(&pdlc->ce);

  if ( ncolor_alloc(context, cb, &p_ncolor) ) {
    /* Allocate storage for command color - copy and update entry */
    HqMemCpy(p_ncolor, pdlc->ce.prc, cb);
    ce_from_ncolor(&pdlc->ce, p_ncolor);

    /* Fill out rest of dl color wrapper */
    pdlc->ci = COLORANTINDEX_UNKNOWN;
    pdlc->cv = (COLORVALUE)0;
  }

  /* Flag succees of creating color */
  return p_ncolor != NULL;

} /* Function dlc_alloc_cmd */


/** \brief Create new dl color entry with paint mask and values.
 *
 * \param cci         Number of colorant values.
 * \param[in] rgci    Array of colorant indexes
 * \param[in] rgcv    Array of colorant values in index order.
 * \param[out] pdlc   Pointer to dl color struct to be filled in.
 *
 * \retval TRUE if created dl color ok
 * \retval FALSE if failed to create DL color. Signals VMERROR and clears pdlc.
 */
Bool dlc_alloc_fillin(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         cci,          /* I */
  /*@notnull@*/ /*@in@*/ const COLORANTINDEX rgci[],
  /*@notnull@*/ /*@in@*/ const COLORVALUE    rgcv[],
  dl_color_t*   pdlc)        /* I */
{
  Bool fAllSep;
  Bool fOk = TRUE;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(cci > 0, "-ve or zero count of colorants");
  HQASSERT(rgci != NULL, "NULL colorant index array address");
  HQASSERT(rgcv != NULL, "NULL colorant values array address");
  HQASSERT(pdlc != NULL, "NULL address for returned dl color details");
  HQASSERT(pdlc->ce.prc == NULL, "dl color entry still refers to a dl color");

#ifdef DLC_INSTRUMENT
  /* Count number of colors created */
  context->gdir.cCreated++;
#endif /* DLC_INSTRUMENT */

  if ( rgci[0] != COLORANTINDEX_NONE ) {
    /* General dl color case - build n color data in buffer */
    ce_build(context, &pdlc->ce, cci, rgci, rgcv);

    /* Try and update cache with one created in buffer */
    fOk = dcc_update(context, &pdlc->ce);

    if ( fOk ) {
      /* Use first colorant for cached values */
      fAllSep = (rgci[0] == COLORANTINDEX_ALL);
      pdlc->ci = rgci[fAllSep ? 1 : 0];
      pdlc->cv = rgcv[fAllSep ? 1 : 0];
    } else { /* Failed to add to cache - clear out the dl color reference */
      dlc_clear(pdlc);
    }
  } else { /* NONE dl color is constant - return copy of dl color entry */
    HQASSERT(cci == 1,
             "dlc_alloc_fillin: more than one colorant index for NONE dl color");

    /* Return NONE dl color constant */
    *pdlc = context->dlc_none;
  }

  return fOk;

} /* Function dlc_alloc_fillin */


/*
 *
 */

static Bool ce_adjust_size(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  color_entry_t * pce, int32 cbpmOld,
  int32 cbpmNew, uint32 * puOldHash)
{
  /* If there is only one reference to the color, and it is the same size before and
     after, there is nothing to do. Otherwise we need to reallocate at the new size,
     and copy the existing content */
  ref_count_t * prcNew;
  int32 cbpmMax, cbHeaderNew, cbHeaderOld, iColorant, ipm;
  paint_mask_t * ppmNew;
  paint_mask_t * ppmOld;
  COLORVALUE * pcvNew;
  COLORVALUE * pcvOld;

  HQASSERT(CE_IS_VALID(pce), "invalid ce in ce_adjust_size");

  if ((* pce->prc) == 1 && cbpmOld == cbpmNew) {
    /* We have to rehash into the cache, because the content of the color is changing
     (which will change the hash value). So remove the entry from the cache */
    * puOldHash = dcc_hash(pce);
    HQASSERT(dcc_find(context, *puOldHash, pce) != NULL,
             "didn't find ce under recorded hash value");
    return FALSE;
  }

  debug_scribble_buffer(context, pce->ccv);
  prcNew = (ref_count_t*)&context->grgbColorBuffer[0];
  * prcNew = (ref_count_t)1;
  * puOldHash = 0u; /* not used, but better not left floating */

  /* Copy either the whole paint mask (leaving the remaining new bytes to be filled
     in later) or only the new bytes portion of it if this is less (in which case
     we'll be dropping a well controlled portion of it on the floor) */

  cbpmMax = cbpmOld;
  if (cbpmMax > cbpmNew)
    cbpmMax = cbpmNew;

  ppmNew = (paint_mask_t*)(prcNew + 1);
  ppmOld = pce->ppm;
  for (ipm = 0; ipm < cbpmMax; ipm++) {
    ppmNew[ipm] =  ppmOld[ipm];
  }

  cbHeaderNew = DLC_HEADERSIZE(cbpmNew);
  cbHeaderOld = DLC_HEADERSIZE(cbpmOld);
  pcvNew = (COLORVALUE *)((uint8*) prcNew + cbHeaderNew);
  pcvOld = (COLORVALUE *)((uint8*) pce->prc + cbHeaderOld);

  for (iColorant = 0; iColorant < pce->ccv; iColorant++) {
    pcvNew[iColorant] = pcvOld[iColorant];
  }

  pce->prc = prcNew;
  pce->ppm = ppmNew;
  pce->pcv = pcvNew;
  /* pce->ccv unchanged */
  pce->iop = -1; /* pce->iop not known yet: */
  /* pce->opacity unchanged */
  /* pce->allsep unchanged */

  return TRUE;
}


/** \brief Utility procedure implementing the common part of
 * \c dlc_merge_overprints() and \c dlc_apply_overprints()
 *
 * \param[in] ppmSource    Paint mask specifying knocked out colors (NULL if
 *                         all knocked out).
 * \param combineOp        One of \c DLC_INTERSECT_OP, \c DLC_UNION_OP, or
 *                         \c DLC_REPLACE_OP determining how to combine
 *                         overprints.
 * \param[in,out] pdlc     Color to be augmented with the overprint information
 *                         from ppm.
 *
 * \retval TRUE if overprint merge successfully done.
 * \retval FALSE if merge failed; \a pdlc is left unchanged.
 */
static Bool dlc_combine_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@null@*/ /*@in@*/ const paint_mask_t *ppmSource,
  int32 combineOp,
  /*@notnull@*/ dl_color_t * pdlc)
{
  paint_mask_t * ppmTarget;
  int16 ipmTarget, ipmSource;
  color_entry_t ce;
  int32 cbSource, cbTarget;
  Bool fRealloc = FALSE;
  uint32 uOldHash = 0u;

  HQASSERT(combineOp == DLC_INTERSECT_OP || combineOp == DLC_UNION_OP ||
           combineOp == DLC_REPLACE_OP, "dlc_combine_overprints: unrecognised combineOp");

  /* locate overprint bits in the target color */
  ppmTarget = pm_from_dlc(pdlc);

  HQASSERT((pm_locate_overprints(ppmTarget, &ipmTarget), ipmTarget == pdlc->ce.iop),
            "inconsistency between iop and pm overprint location");

  ipmTarget = pdlc->ce.iop;

  /* - If the color does not already have overprint information, augment the color with it
     - If it has SHFILL overprints and we are being given MAXBLT overprints,
       or combineOp is DLC_REPLACE_OP, replace overprint bits with new ones
     - Otherwise form the intersection or union of the overprints, according to

       combineOp.
     - If ppmSource is NULL, this is equivalent to the universal set, so clear all
       overprints. It is easier at this point to simply clear bits rather than remove
       them altogether, since that doesn't require any memory re-allocation (providing
       there is only a single reference to the color)
   */

  cbSource = 0;
  if (ppmSource != NULL) {
    cbSource = (*ppmSource == 0
                ? 1 /* not cmd, just an empty mask */
                : pm_findsize(ppmSource));
    HQASSERT(cbSource > 0, "empty ppmSource overprint bit-set");
  }

  ce = pdlc->ce;

  if (ipmTarget < 0) {

    /* if ppmSource were NULL, there's nothing to do - no overprints to clear out */
    if (ppmSource == NULL)
      return TRUE;

    cbTarget = pm_findsize(ppmTarget);
    HQASSERT(cbTarget > 0, "no colors in ppmTarget");
    HQASSERT(ce.iop < 0, "iop already set");
    fRealloc = ce_adjust_size(context, &ce, cbTarget,
                              cbTarget + cbSource + PM_SIZE_CMD, &uOldHash);
    ppmTarget = ce.ppm;

    /* insert the source mask and associated escape commands */

    ipmTarget = CAST_SIGNED_TO_INT16(cbTarget - 1);
    ppmTarget[ipmTarget] = (uint8) (ppmTarget[ipmTarget] | PM_CHAIN);
    ipmTarget++;
    ppmTarget[ipmTarget++] = PM_CMD;
    ppmTarget[ipmTarget++] = PM_CMD_OPMAXBLT;
    ce.iop = ipmTarget;
    ipmSource = 0;
    do {
      ppmTarget[ipmTarget] = ppmSource[ipmSource++];
    } while (PM_CHAIN_SET (ppmTarget[ipmTarget++]));

  } else if ( combineOp == DLC_REPLACE_OP ) {
    /* Replace the Target overprint information with the Source, since MAXBLT
       information takes precedence. This is the same as if we had no overprinting
       information already, as above, except that we have to get rid of the existing
       overprinting information first */

    /* we've already got the command bytes, so we just need the extra source bytes -
       or if there are none, to lose the command bytes altogether */

    /* Must always calculate mask length from start.
       A present but empty op mask may otherwise be mistaken for a cmd */
    cbTarget = pm_findsize(ppmTarget);

    if (cbSource > 0) {

      fRealloc = ce_adjust_size(context, &ce, cbTarget, ipmTarget + cbSource,
                                &uOldHash);
      ppmTarget = ce.ppm;

      /* and insert the overprint information as above */

      ppmTarget[ipmTarget - 1] = PM_CMD_OPMAXBLT;
      ipmSource = 0;
      do {
        ppmTarget[ipmTarget] = ppmSource[ipmSource++];
      } while (PM_CHAIN_SET (ppmTarget[ipmTarget++]));

      /* ce.iop unchanged */
      ce.iop = pdlc->ce.iop;
      ce.opacity = pdlc->ce.opacity;
    } else {
      int32 cbpmNew = ipmTarget - PM_SIZE_CMD ;
      fRealloc = ce_adjust_size(context, &ce, cbTarget, cbpmNew, &uOldHash);
      ppmTarget = ce.ppm;
      /* Need to remove the chain bit from the last pm (since size reduced). */
      PM_CLEAR_CHAIN (ppmTarget[cbpmNew-1]) ;
    }

    HQASSERT((pm_locate_overprints(ce.ppm, &ipmTarget), ipmTarget == ce.iop),
             "inconsistency between iop and pm overprint location");

  } else if ( combineOp == DLC_INTERSECT_OP ) {
    /* produce an intersection of the overprint information. Cases:
       - ppmSource and ppmTarget the same length: AND ppmSource into ppmTarget
       - ppmSource is shorter: AND the common part and clear any remaining bytes
       - ppmSource is longer: AND the common part only
     */

    /* in the case when more than one client has a handle on it, we need a new color
       even if it would fit , otherwise we'd be changing a color under someone's
       feet. It is therefore well worth checking whether the operation would actually
       change anything first. Also note that if the source explicitly has no
       overprints (cbSource == 0), we do not lose the overprint information, we just
       zero it. This is because a color with no overprint information would be
       augmented with new overprint information next time some is given, but with
       zero overprints would continually produce the empty set */

    /* Must always calculate mask length from start.
       A present but empty op mask may otherwise be mistaken for a cmd */
    cbTarget = pm_findsize(ppmTarget);

    /* if we get here, we know the target paint mask content will change */

    fRealloc = ce_adjust_size(context, &ce, cbTarget, cbTarget, &uOldHash);
    ppmTarget = ce.ppm;

    ipmSource = 0;
    do {
      if (ppmSource != NULL) {
        ppmTarget[ipmTarget] = (paint_mask_t) (ppmTarget[ipmTarget] &
                                         (ppmSource[ipmSource] | PM_CHAIN));
        if (! PM_CHAIN_SET (ppmSource[ipmSource++]))
          ppmSource = NULL;
      } else {
        ppmTarget[ipmTarget] = (paint_mask_t) (ppmTarget[ipmTarget] & PM_CHAIN);
      }
    } while (PM_CHAIN_SET (ppmTarget[ipmTarget++]));

    ce.iop = pdlc->ce.iop;
    HQASSERT((pm_locate_overprints(ce.ppm, &ipmTarget), ipmTarget == ce.iop),
             "inconsistency between iop and pm overprint location");
    ce.opacity = pdlc->ce.opacity;
  }
  else {
    /* produce a union of the overprint information. Cases:
       - ppmSource and ppmTarget the same length: OR ppmSource into ppmTarget
       - ppmSource is shorter: OR the common part and copy any remaining bytes
       - ppmSource is longer: OR the common part and copy any remaining bytes
     */

    /* if ppmSource were NULL, there's nothing to do - no overprints to union */
    if (ppmSource == NULL)
      return TRUE;

    /* in the case when more than one client has a handle on it, we need a new color
       even if it would fit , otherwise we'd be changing a color under someone's
       feet. It is therefore well worth checking whether the operation would actually
       change anything first */

    /* Must always calculate mask length from start.
       A present but empty op mask may otherwise be mistaken for a cmd */
    cbTarget = pm_findsize(ppmTarget);

    /* if we get here, we know the target paint mask content will change */

    { int32 cbpmNew = cbTarget ;
      if ( ipmTarget + cbSource > cbpmNew )
        cbpmNew = ipmTarget + cbSource ;
      fRealloc = ce_adjust_size(context, &ce, cbTarget, cbpmNew, &uOldHash);
      ppmTarget = ce.ppm;
    }

    ipmSource = 0;
    while ( PM_CHAIN_SET (ppmTarget[ipmTarget] ) &&
            PM_CHAIN_SET (ppmSource[ipmSource] ))
      ppmTarget[ipmTarget++] |= ppmSource[ipmSource++] ;
    ppmTarget[ipmTarget++] |= ppmSource[ipmSource++] ;
    while ( PM_CHAIN_SET (ppmSource[ipmSource-1] ))
      ppmTarget[ipmTarget++] = ppmSource[ipmSource++] ;

    ce.iop = pdlc->ce.iop;
    HQASSERT((pm_locate_overprints(ce.ppm, &ipmTarget), ipmTarget == ce.iop),
             "inconsistency between iop and pm overprint location");
    ce.opacity = pdlc->ce.opacity;
  }

  /* either re-hash the color, or allocate the memory and free up the old entry
     according to whether we changed the color in situ or modified a shuffled copy */

  if (! fRealloc) {
    HQASSERT(pdlc->ce.prc == ce.prc, "pointers to ncolor unexpectedly different");
    dcc_rehash(context, &pdlc->ce, uOldHash);
  } else {
    if ( !dcc_update(context, &ce) )
      return FALSE;
    dlc_release(context, pdlc);
    pdlc->ce = ce;
    pdlc->ci = COLORANTINDEX_UNKNOWN;
    pdlc->cv = 0;
  }

  return TRUE;
}


/** \brief Extend given dl color to include details of which colorants would be
 *         knocked out if a nonintercepted color calculation had been done.
 *
 * \param overprintsType      One of \c DLC_MAXBLT_OVERPRINTS or
 *                            \c DLC_MAXBLT_KNOCKOUTS, indicating what kind of
 *                            overprinting the function applies to.
 *                            \c DLC_MAXBLT_KNOCKOUTS are used when no overprint
 *                            reduction is done on an intercepting color chain.
 *                            It prevents overprinting the set of colors which
 *                            are in both the DL color given and the colorant
 *                            indexes.
 * \param combineOp           Either \c DLC_UNION_OP, \c DLC_INTERSECT_OP or
 *                            \c DLC_REPLACE_OP.
 * \param cci                 Number of colorant indexes.
 * \param[in] rgci            Array of (sorted) colorant indexes, representing
 *                            those colorants which would be overprinted
 *                            (*not* those knocked out).
 *
 * \retval TRUE if successfully done
 * \retval FALSE if fails; pdlc is left unchanged
 *
 * \note        - Colorant indexes must be sorted and not include any duplicates
 *              - Colorant indexes must be a subset of those already
 *                present in the given color
 *              - cci could be zero (indicating that _no_ colors overprint), but not negative
 */
Bool dlc_apply_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32         overprintsType, /* I */
  int32         combineOp,      /* I */
  int32         cci,            /* I */
  /*@notnull@*/ /*@in@*/ const COLORANTINDEX rgci[],
  /*@notnull@*/ dl_color_t *pdlc)
{
  /* For a single very large colorant index, we could in principle use all but a byte
     or two for mask data, but we won't be any worse off if we have a temporary mask at
     least as long */

  paint_mask_t grpm[sizeof (context->grgbColorBuffer)];
  paint_mask_t * ppm = grpm;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(cci >= 0, "dlc_apply_overprints: cci < 0");
  HQASSERT(pdlc != NULL, "dlc_apply_overprints: NULL pdlc");
  HQASSERT(pdlc->ce.prc != NULL, "dlc_apply_overprints: NULL pdlc->ce.prc");
  HQASSERT(cci == 0 || pm_in_order(cci, rgci),
           "dlc_apply_overprints: colorant indexes are not in order");
  HQASSERT(combineOp == DLC_INTERSECT_OP || combineOp == DLC_UNION_OP ||
           combineOp == DLC_REPLACE_OP,
           "dlc_apply_overprints: unrecognised combineOp");

  /* construct a paintmask-style bitset according to the colorants given, having
     checked the special cases where no colors overprint */

  if (cci == 0) {
    ppm = NULL;
  } else {
    int32 cciPad;

    (void)pm_setup( ppm, cci, rgci, & cciPad, COLORVALUE_ONE );
    /* don't care about result byte count at this stage */
    HQASSERT(cciPad == 0,
      "dlc_apply_overprints: padding colorants returned, indicating unexpected All separation");
  }

  /* use the mask constructed to apply to the given color */
  switch ( overprintsType ) {
  case DLC_MAXBLT_OVERPRINTS:
    break ;
  case DLC_MAXBLT_KNOCKOUTS:
    /* If the pm created is the same as the pm of the dlc then we need do nothing.
     * Otherwise we need to modify it to be pm = ~pm & pm(dlc).
     */
    if ( ppm != NULL && pm_equal( ppm , pm_from_dlc( pdlc )))
      return TRUE ;

    { /* Merge paint masks. */
      int32 ipm = 0, ipmLastNonEmpty = 0 ;
      paint_mask_t *rgpmSrc = pm_from_dlc( pdlc ) ;
      paint_mask_t *rgpmMod = ppm ;
      paint_mask_t *rgpmDst = grpm ;
      do {
        paint_mask_t pmSrc = rgpmSrc[ ipm ] ;
        paint_mask_t pmMod = ( paint_mask_t )( rgpmMod != NULL ? rgpmMod[ ipm ] : 0x00 ) ;
        paint_mask_t pmDst = ( paint_mask_t )( pmSrc & ~pmMod ) ;
        PM_SET_CHAIN( pmDst ) ;
        rgpmDst[ ipm ] = pmDst ;
        if ( PM_GET_MASK( pmDst ) != 0 )
          ipmLastNonEmpty = ipm ;
        if ( ! PM_CHAIN_SET( pmMod ))
          rgpmMod = NULL ;
        ++ipm ;
      } while ( PM_CHAIN_SET( rgpmSrc[ ipm - 1 ] ) && rgpmSrc[ ipm ] != PM_CMD ) ;

      PM_CLEAR_CHAIN( rgpmDst[ ipmLastNonEmpty ] ) ;
      if ( rgpmDst[ ipmLastNonEmpty ] == 0 ) {
        HQFAIL( "Paint mask should not be empty - skipping applying max-blits" ) ;
        return TRUE ;
      }
    }
    ppm = grpm ;
    break ;
  default:
    HQFAIL( "Unknown overprintsType" ) ;
  }

  return dlc_combine_overprints(context, ppm, combineOp, pdlc);
}


/** \brief Determines whether the color given incorporates maxblt style
 * overprint information.
 *
 * \param[in] pdlc  The color to be queried.
 *
 * \retval TRUE if maxblits present
 * \retval FALSE if maxblits not present
 */
Bool dlc_doing_maxblt_overprints(
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc)
{
  paint_mask_t * ppm;
  int16 ipmOverprint;

  HQASSERT(pdlc != NULL, "pdlc NULL in dlc_doing_maxblt_overprints");
  HQASSERT((pm_locate_overprints(pm_from_dlc(pdlc), &ipmOverprint),
            ipmOverprint == pdlc->ce.iop),
           "inconsistency between iop and pm overprint location");

  ipmOverprint = pdlc->ce.iop;

  if (ipmOverprint < 0)
    return FALSE;

  ppm = pm_from_dlc(pdlc);

  return ppm[ipmOverprint - 1] == PM_CMD_OPMAXBLT;
}

/** \brief Takes the color's associated overprinting information (if any) and
 * culls all colorants recorded as being overprinted. However, it only does
 * this when the overprints are not DLC_MAXBLT_OVERPRINTS.
 *
 * \param[in,out] pdlc  The color to be reduced
 *
 * \retval TRUE if successfully done
 * \retval FALSE if fails; pdlc is left unchanged
 */
Bool dlc_reduce_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t* pdlc)
{
  dl_color_t dlc_reduced;
  dl_color_iter_t dlci;
  dlc_iter_result_t iter_res;
  COLORANTINDEX ci;
  COLORVALUE cv;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);

  /* The max-blt case is completely separate.  No COLORVALUE_TRANSPARENT
     are present, colors instead contain an overprint paint mask.  The
     max-blt overprints are dealt with during rendering. */
  if (pdlc->ce.iop >= 0) {
#if defined(ASSERT_BUILD)
    for (iter_res = dlc_first_colorant(pdlc, &dlci, &ci, &cv);
         iter_res == DLC_ITER_COLORANT;
         iter_res = dlc_next_colorant(pdlc, &dlci, &ci, &cv)) {
      /* COLORVALUE_TRANSPARENT is only set when doing overprint reduction,
         which does not occur when intercepting and max-blting. */
      HQASSERT(cv != COLORVALUE_TRANSPARENT,
               "Should not have COLORVALUE_TRANSPARENT when max-blting");
    }
#endif
    return TRUE;
  }

  dlc_clear(&dlc_reduced);

  if ( !dlc_copy(context, &dlc_reduced, pdlc) )
    return FALSE;

  for (iter_res = dlc_first_colorant(pdlc, &dlci, &ci, &cv);
       iter_res == DLC_ITER_COLORANT;
       iter_res = dlc_next_colorant(pdlc, &dlci, &ci, &cv)) {
    if (cv == COLORVALUE_TRANSPARENT) {
      if ( !dlc_remove_colorant(context, &dlc_reduced, ci) ) {
        dlc_release(context, &dlc_reduced);
        return FALSE;
      }
    }
  }

  if (dlc_reduced.ce.prc == pdlc->ce.prc) {
    /* Didn't have to reduce anything */
    dlc_release(context, &dlc_reduced);
  } else {
    /* dlc_reduced is a new color */
    dlc_release(context, pdlc);
    *pdlc = dlc_reduced;
  }

  return TRUE;
}

/** \brief Clear the max-blt overprint information from the dl color.
 */
Bool dlc_clear_overprints(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t *dlc)
{
  color_entry_t ce = dlc->ce;
  int32 cbpm_new;
  uint32 old_hash = 0;
  Bool realloc;

  if ( ce.iop < 0 )
    return TRUE; /* No overprint info to clear. */

  cbpm_new = ce.iop - PM_SIZE_CMD;

  realloc = ce_adjust_size(context, &ce, pm_findsize(pm_from_dlc(dlc)),
                           cbpm_new, &old_hash);
  HQASSERT(ce.iop == -1, "Overprints should have been cleared");

  /* Need to remove the chain bit from the last pm (since size reduced). */
  PM_CLEAR_CHAIN(ce.ppm[cbpm_new-1]) ;

  if ( !realloc ) {
    HQASSERT(dlc->ce.prc == ce.prc, "pointers to ncolor unexpectedly different");
    dcc_rehash(context, &dlc->ce, old_hash);
  } else {
    if ( !dcc_update(context, &ce) )
      return FALSE;
    dlc_release(context, dlc);
    dlc->ce = ce;
    dlc->ci = COLORANTINDEX_UNKNOWN;
    dlc->cv = 0;
  }

  return TRUE;
}

/** \brief For a given colorant index determines whether the color is
 * potentially overprinted.
 *
 * This is the case if either the colorant is not present at all, or if it is
 * present by marked in the MAXBLT overprint flags as overprinted
 *
 * \param[in] pdlc        The color to examine
 * \param ci              The colorant in question
 *
 * \retval TRUE if potentially MAXBLT or overprinted
 * \retval FALSE if not MAXBLT and not overprinted
 */
Bool dlc_colorant_is_overprinted(
          /*@notnull@*/ /*@in@*/ const dl_color_t * pdlc,
                                 COLORANTINDEX ci)
{
  int32 icv;

  HQASSERT(ci >= 0, "Can only check for overprints of real colorants") ;

  if (pdlc->ci != ci) /* it usually will be, given the context of the
                         call, so no need to check again if so, but if
                         we do and it is not present then it must be
                         overprinted */
    if (! dlc_set_indexed_colorant(pdlc, ci))
      return TRUE;

  /* If there is no overprint information, but the color exists, we
     can't be overprinting; cross check for consistency */

#if defined( ASSERT_BUILD )
  {
    int16 ipm;
    pm_locate_overprints(pdlc->ce.ppm, &ipm);
    HQASSERT(ipm == pdlc->ce.iop, "inconsistency between iop and pm overprint location");
  }
#endif

  /* No overprint information, it was never there or has since been cleared. */
  if (pdlc->ce.iop < 0 || pdlc->ce.ppm[pdlc->ce.iop] == 0)
    return FALSE;

  /* The colorant being present indicates it _is_ overprinting */
  return pm_colorant_offset(pdlc->ce.ppm + pdlc->ce.iop, ci, & icv /* not used */);
}

/** \brief Copy dl color data pointer and track ref count.
 *
 * \param[out] pp_ncolor_dest    Pointer to dl color pointer to copy to.
 * \param[in] pp_ncolor_src      Pointer to dl color pointer to copy from
 *
 * \retval TRUE if copy successfully done
 * \retval FALSE if fails, \a pp_ncolor_dest is set to NULL
 */
Bool dl_copy(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ p_ncolor_t *pp_ncolor_dest,
  /*@notnull@*/ /*@in@*/ p_ncolor_t *pp_ncolor_src)
{
  Bool fCopyOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor_dest != NULL, "NULL pointer to destination n color pointer");
  HQASSERT(pp_ncolor_src != NULL, "NULL pointer to source n color pointer");
  HQASSERT(*pp_ncolor_src != NULL, "NULL pointer to source n color data");

  /* Copy pointer to color data */
  *pp_ncolor_dest = *pp_ncolor_src;

  /* Copy ok if black or white constants or got reference */
  fCopyOk = DL_REFS_CONSTANT(context, *pp_ncolor_src) ||
            dl_get_reference(context, pp_ncolor_src);

  if ( ! fCopyOk )
    *pp_ncolor_dest = NULL ;

  return fCopyOk;

} /* Function dl_copy */


/** \brief Copy color info between wrapper and release reference in source
 *
 * \param[out] pdlcDest    Pointer to destination dl color wrapper.
 * \param[in] pdlcSrc      Pointer to source dl color wrapper.
 *
 * \note No change in number of references so no copy on max out required.
 */
void dlc_copy_release(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlcDest,
  /*@notnull@*/ /*@in@*/ dl_color_t *pdlcSrc)
{
  HQASSERT(pdlcDest != NULL, "NULL pointer to destination dl color");
  HQASSERT(pdlcSrc != NULL, "NULL pointer to source dl color");
  HQASSERT(!DLC_IS_CONSTANT(context, pdlcDest), "destination is constant black or white");

  UNUSED_PARAM(dlc_context_t*, context);

  /* Copy dl color info across and empty old wrapper */
  *pdlcDest = *pdlcSrc;
  dlc_clear(pdlcSrc);

} /* Function dlc_copy_release */


/** \brief Copy one dl color from source to destination color wrapper
 *
 * \param pdlcDest    Pointer to destination dl color wrapper.
 * \param pdlcSrc     Pointer to source dl color wrapper.
 *
 * \retval TRUE if dl color entry succesfully copied
 * \retval FALSE if failed.
 *
 * \note        We get a reference on the orginal dl color reference
 *              since experience shows this gets copied around more,
 *              so it is better (read more efficient) to have this
 *              getting a new copy of the color with a reset ref count.
 */
Bool dlc_copy(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*    pdlcDest,   /* I/O */
  dl_color_t*    pdlcSrc)    /* I */
{
  Bool fCopyOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlcDest != NULL, "NULL pointer to destination dl color");
  HQASSERT(pdlcSrc != NULL, "NULL pointer to source dl color");
  HQASSERT(!DLC_IS_CONSTANT(context, pdlcDest), "destination is constant black or white");
  HQASSERT(pdlcSrc->ce.prc != NULL, "NULL pointer to start of source color");

  /* Copy wrapper structure details */
  *pdlcDest = *pdlcSrc;

  /* Copy ok if black or white constants or got reference */
  fCopyOk = DLC_REFS_CONSTANT(context, pdlcSrc) ||
            dlc_get_reference(context, pdlcSrc);

  if ( ! fCopyOk )
    dlc_clear( pdlcDest ) ;

  return fCopyOk;

} /* Function dlc_copy */


/** \brief Create a new DL color based on template and values.
 *
 * \param[out] pdlcDest Pointer to dl color entry to setup
 * \param[in] pdlcSrc   Pointer to dl color entry to use as template
 * \param[in] rgcv      Array of color values to use for new dl color entry
 * \param ccv           Count of color values in rgcv
 *
 * \retval TRUE if created dl color ok
 * \retval FALSE if failed. Signals VMERROR and clears pdlcDest.
 */
Bool dlc_alloc_fillin_template(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlcDest,
  /*@notnull@*/ /*@in@*/ const dl_color_t *pdlcSrc,
  /*@notnull@*/ /*@in@*/ const COLORVALUE rgcv[],
  int32 ccv)
{
  Bool fOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlcDest != NULL, "NULL pointer to destination dl color");
  HQASSERT(pdlcDest->ce.prc == NULL, "dl color entry still refers to a dl color");
  HQASSERT(pdlcSrc != NULL, "NULL pointer to source dl color");
  HQASSERT(rgcv != NULL, "NULL pointer to array of colors");
  HQASSERT(ccv == pdlcSrc->ce.ccv, "wrong number of values passed in");

#ifdef DLC_INSTRUMENT
  /* Count number of colors created */
  context->gdir.cCreated++;
#endif /* DLC_INSTRUMENT */

  /* Build new n color entry with new colorvalues */
  ce_build_template(context, &pdlcDest->ce, &pdlcSrc->ce);
  HqMemCpy(pdlcDest->ce.pcv, rgcv, ccv*sizeof(COLORVALUE));

  /* Update cache with new color entry*/
  fOk = dcc_update(context, &pdlcDest->ce);

  if ( fOk ) {
    /* Cache updated ok - finish of wrapper details */
    pdlcDest->ci = COLORANTINDEX_UNKNOWN;
    pdlcDest->cv = (COLORVALUE)0;

  } else { /* Failed to add to cache - clear out the dl color reference */
    dlc_clear(pdlcDest);
  }

  return fOk;

} /* Function dlc_alloc_fillin_template */


/** \brief Interpolate color values from dl colors into new dl color
 *
 * \param n_weights      Number of color values to copy
 * \param[in] rgrWeights Array of color values to set dl color to
 * \param[out] pdlcDest  Pointer to dl color to set color values of.
 * \param[in] rgpdlcSrc  Pointer to dl colors to interpolate.
 *
 * \retval TRUE if created dl color ok
 * \retval FALSE if failed. Signals VMERROR and clears pdlcDest.
 */
Bool dlc_alloc_interpolate(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  int32 n_weights,
  /*@notnull@*/ /*@in@*/ const SYSTEMVALUE rgrWeights[],
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlcDest,
  /*@notnull@*/ /*@in@*/ dl_color_t *const rgpdlcSrc[])
{
  int32 wi;
  int32 icv;
  Bool fOk;
  Bool opacityPresent;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlcDest != NULL, "NULL pointer to dl color");
  HQASSERT(pdlcDest->ce.prc == NULL, "dl color entry still refers to a dl color");
  HQASSERT(rgpdlcSrc != NULL && rgpdlcSrc[0] != NULL, "NULL pointer to dl color");
  HQASSERT(n_weights > 0, "not enough weights to interpolate with");
  HQASSERT(rgrWeights != NULL, "NULL pointer to weights array");
  HQASSERT(rgpdlcSrc[0]->ce.prc != NULL, "NULL pointer to start of source color");

#if defined( ASSERT_BUILD )
  {
    SYSTEMVALUE total_weight = 0 ;
    static Bool debug_dlc_interpolate = TRUE ;

    for ( wi = 0 ; wi < n_weights ; ++wi ) {
      total_weight += rgrWeights[wi] ;
    }
    HQASSERT(fabs(1 - total_weight) < EPSILON,
             "dlc_alloc_interpolate: Total interpolation weight not unity") ;

    /* Check paint masks all have the same colorants. */
    if ( debug_dlc_interpolate ) {
      COLORVALUE        cv ;
      COLORANTINDEX     ci ;
      dl_color_iter_t   dlci ;
      dlc_iter_result_t iter_res ;

      for ( iter_res = dlc_first_colorant( rgpdlcSrc[ 0 ] , & dlci , & ci , & cv ) ;
            iter_res == DLC_ITER_COLORANT ;
            iter_res = dlc_next_colorant ( rgpdlcSrc[ 0 ] , & dlci , & ci , & cv )) {
        for ( wi = 1; wi < n_weights; wi++ )
          HQASSERT(dlc_set_indexed_colorant( rgpdlcSrc[ wi ] , ci ) ,
                    "dlc_alloc_interpolate: missing colorant" ) ;
      }
    }

    /* Check the opacity fields match each other */
    for ( wi = 0; wi < n_weights; wi++ ) {
      COLORVALUE cv ;
      Bool opacityPresentCheck = ( rgpdlcSrc[wi]->ce.opacity < COLORVALUE_ONE ) ;
      HQASSERT(opacityPresentCheck ==
          dlc_get_indexed_colorant_( rgpdlcSrc[wi], COLORANTINDEX_ALPHA, &cv ),
          "dl color with inconsistent opacity") ;
      if ( !opacityPresentCheck )
        cv = COLORVALUE_ONE ;
      HQASSERT(rgpdlcSrc[wi]->ce.opacity == cv, "dl color with inconsistent opacity") ;
    }
  }
#endif

#ifdef DLC_INSTRUMENT
  /* Count number of colors created */
  context->gdir.cCreated++;
#endif /* DLC_INSTRUMENT */

  /* If any source color contains opacity, then the dest color must be based on
     a color with opacity */
  for ( wi = 0; wi < n_weights; ++wi ) {
    if ( dlc_color_opacity( rgpdlcSrc[wi] ) < COLORVALUE_ONE )
      break ;
  }
  opacityPresent = ( wi < n_weights );
  if ( !opacityPresent )
    wi = 0 ;

  /* Build compatible color entry */
  ce_build_template(context, &pdlcDest->ce, &rgpdlcSrc[wi]->ce);

  /* Calculate interpolate color values for new color entry for each colorant.
     NB. ce.ccv includes opacity when present. */
  for ( icv = 0;
        opacityPresent ? icv < pdlcDest->ce.ccv - 1 : icv < pdlcDest->ce.ccv;
        ++icv ) {
    SYSTEMVALUE scv = 0;

    for ( wi = 0; wi < n_weights; ++wi ) {
      scv += rgrWeights[wi]*(rgpdlcSrc[wi]->ce.pcv[icv]);
    }
    pdlcDest->ce.pcv[icv] = (COLORVALUE)(scv + 0.5);
  }

  /* Interpolate opacity value for new color entry. Set both opacity fields
     which must be updated in tandem. */
  if ( opacityPresent ) {
    SYSTEMVALUE opacity = 0;
    for ( wi = 0; wi < n_weights; ++wi ) {
      opacity += rgrWeights[wi]*(rgpdlcSrc[wi]->ce.opacity);
    }
    pdlcDest->ce.opacity = (COLORVALUE)(opacity + 0.5);
    pdlcDest->ce.pcv[pdlcDest->ce.ccv - 1] = pdlcDest->ce.opacity;
  }

  /* Update cache with new color entry */
  fOk = dcc_update(context, &pdlcDest->ce);

  if ( fOk ) {
    /* Reset indexed color cache for later lazy update */
    pdlcDest->ci = COLORANTINDEX_UNKNOWN;
    pdlcDest->cv = (COLORVALUE)0;

  } else { /* Failed to add to cache - clear out the dl color reference */
    dlc_clear(pdlcDest);
  }

  return fOk;

} /* Function dlc_alloc_interpolate */


/** \brief Compare two dl colors for equality.
 *
 * \param p_ncolor1     Pointer to first color to compare
 * \param p_ncolor2     Pointer to second color to compare
 *
 * \retval TRUE if colors are equal
 * \retval FALSE if colors are not equal
 */
Bool dl_equal(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor1,
 /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor2)
{
  Bool          fEqual;
  int32         cbpm;
  int32         cbHeader;
  int32         cColorants;
  int32         icv;
  const paint_mask_t* ppm1;
  const paint_mask_t* ppm2;
  COLORVALUE*   rgcv1;
  COLORVALUE*   rgcv2;

  HQASSERT(p_ncolor1 != NULL, "NULL pointer to first dl color");
  HQASSERT(p_ncolor2 != NULL, "NULL pointer to second dl color");

  /* Do obvious alias test first */
  fEqual = (p_ncolor1 == p_ncolor2);

  if ( !fEqual ) {
    /* Not aliases so onto general equality test - first thing paint masks */

    ppm1 = pm_from_ncolor(p_ncolor1);
    ppm2 = pm_from_ncolor(p_ncolor2);
    fEqual = pm_equal(ppm1, ppm2);

    if ( fEqual ) {
      /* Paint masks are equal - now need to compare colorvalues */

      /* Get offset to start of colorvalues */
      cbpm = pm_findsize(ppm1);
      cbHeader = DLC_HEADERSIZE(cbpm);
      rgcv1 = (COLORVALUE*)((paint_mask_t*)p_ncolor1 + cbHeader);
      rgcv2 = (COLORVALUE*)((paint_mask_t*)p_ncolor2 + cbHeader);

      /* Find total number of colorvalues */
      cColorants = pm_total_colorants(ppm1);

      for ( icv = 0; fEqual && (icv < cColorants); icv++ ) {
        fEqual = (rgcv1[icv] == rgcv2[icv]);
      }
    }
  }

  return fEqual;

} /* Function dl_equal */


/** \brief Compare two dl colors for equality.
 *
 * \param pdlc1       Pointer to first dl color
 * \param pdlc2       Pointer to second dl color
 *
 * \retval TRUE if the two dl colors have equal colorant paint masks and values
 * \retval FALSE if dl colors are different.
 *
 * \note Currently there is an unecessary overhead when there is /All sep
 * default colorvalues
 */
Bool dlc_equal(
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc1,
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc2)
{
  int32   icv;
  Bool    fEqual;

  HQASSERT(pdlc1 != NULL, "NULL pointer to first dl color entry");
  HQASSERT(pdlc1->ce.prc != NULL, "NULL pointer to first dl color entry paint mask");
  HQASSERT(pdlc1->ce.pcv != NULL, "NULL pointer to first dl color entry colorant values");
  HQASSERT(pdlc2 != NULL, "NULL pointer to second dl color entry");
  HQASSERT(pdlc2->ce.prc != NULL, "NULL pointer to second dl color entry paint mask");
  HQASSERT(pdlc2->ce.pcv != NULL, "NULL pointer to second dl color entry colorant values");

  /* Check for dl color entry or color data aliases */
  fEqual = (pdlc1 == pdlc2) || (pdlc1->ce.prc == pdlc2->ce.prc);

  if ( !fEqual ) {
    /* Got distinct data - now check num channels and paint masks */

    fEqual = (pdlc1->ce.ccv == pdlc2->ce.ccv) &&
                pm_equal(pm_from_dlc(pdlc1), pm_from_dlc(pdlc2));

    if ( fEqual ) {
      /* Paint masks are equal - check all colorant values */
      for ( icv = 0; fEqual && (icv < pdlc1->ce.ccv); icv++ ) {
        fEqual = (pdlc1->ce.pcv[icv] == pdlc2->ce.pcv[icv]);
      }
    }
  }

  return fEqual;

} /* Function dlc_equal */


/** \brief Compare two dl colors for having the same colorants
 * (but not necessarily the same values).
 *
 * \param p_ncolor1     Pointer to first color to compare
 * \param p_ncolor2     Pointer to second color to compare
 *
 * \retval TRUE if the two dl colors have equal colorants paint masks
 * \retval FALSE if two dl colors have different colorant sets
 *
 * \note Currently there is an unecessary overhead when there is /All sep
 * default colorvalues.
 */
Bool dl_equal_colorants(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t  p_ncolor1,
 /*@notnull@*/ /*@in@*/ const p_ncolor_t  p_ncolor2)
{
  Bool   fEqual;
  paint_mask_t* ppm1;
  paint_mask_t* ppm2;

  HQASSERT(p_ncolor1 != NULL, "NULL pointer to first dl color");
  HQASSERT(p_ncolor2 != NULL, "NULL pointer to second dl color");

  /* Do obvious alias test first */
  fEqual = (p_ncolor1 == p_ncolor2);

  if ( !fEqual ) {
    /* Not aliases so onto general equality test - first thing paint masks */

    ppm1 = pm_from_ncolor(p_ncolor1);
    ppm2 = pm_from_ncolor(p_ncolor2);
    fEqual = pm_equal(ppm1, ppm2);
  }

  return fEqual;

} /* Function dl_equal_colorants */


/** \brief Compare two dl colors for having the same colorants
 * (but not necessarily the same values).
 *
 * \param pdlc1       Pointer to first dl color
 * \param pdlc2       Pointer to second dl color
 *
 * \retval TRUE if the two dl colors have equal colorants paint masks
 * \retval FALSE if dl colors have different colorants
 *
 * \note Currently there is an unecessary overhead when there is /All sep
 * default colorvalues.
 */
Bool dlc_equal_colorants(
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc1,
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc2)
{
  Bool fEqual;

  HQASSERT(pdlc1 != NULL, "NULL pointer to first dl color entry");
  HQASSERT(pdlc1->ce.prc != NULL, "NULL pointer to first dl color entry paint mask");
  HQASSERT(pdlc1->ce.pcv != NULL, "NULL pointer to first dl color entry colorant values");
  HQASSERT(pdlc2 != NULL, "NULL pointer to second dl color entry");
  HQASSERT(pdlc2->ce.prc != NULL, "NULL pointer to second dl color entry paint mask");
  HQASSERT(pdlc2->ce.pcv != NULL, "NULL pointer to second dl color entry colorant values");

  /* Check for dl color entry or color data aliases */
  fEqual = (pdlc1 == pdlc2) || (pdlc1->ce.prc == pdlc2->ce.prc);

  if ( !fEqual ) {
    /* Got distinct data - now check num channels and paint masks */

    fEqual = (pdlc1->ce.ccv == pdlc2->ce.ccv) &&
                pm_equal(pm_from_dlc(pdlc1), pm_from_dlc(pdlc2));
  }

  return fEqual;

} /* Function dlc_equal_colorants */

Bool dlc_merge_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*        pdlc1,     /* I/O */
  dl_color_t*        pdlc2,     /* I */
  dlc_merge_action_t action)    /* I */
{
  color_entry_t     ce;
  Bool              fMergeOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc1, "pdlc1 is null");
  HQASSERT(pdlc2, "pdlc2 is null");

  if ( pdlc1->ce.ppm[ 0 ] == PM_CMD ) {
    switch ( pdlc1->ce.ppm[ 1 ] ) {
    case PM_CMD_ALL0 : /* black */
      if ( pdlc2->ce.ppm[ 0 ] != PM_CMD || pdlc2->ce.ppm[ 1 ] != PM_CMD_ALL0 ) {
        dlc_release(context, pdlc1);
        *pdlc1 = context->dlc_black;
      }
      return TRUE ;
    case PM_CMD_ALL1 : /* white */
      return TRUE ;
    case PM_CMD_NONE :
      /* No need to do a release on dl none color. */
      return dlc_copy(context, pdlc1, pdlc2) ;
    }
  }
  if ( pdlc2->ce.ppm[ 0 ] == PM_CMD ) {
    switch ( pdlc2->ce.ppm[ 1 ] ) {
    case PM_CMD_ALL0 :
    case PM_CMD_ALL1 :
      dlc_release(context, pdlc1);
      *pdlc1 = context->dlc_black;
      return TRUE ;
    case PM_CMD_NONE :
      return TRUE ;
    }
  }

  fMergeOk = ce_merge(context, &ce, &pdlc1->ce, &pdlc2->ce, action);
  HQASSERT(fMergeOk, "ce_merge should not fail in this case");

  if ( fMergeOk ) {

    fMergeOk = dcc_update(context, &ce);

    if ( fMergeOk ) {
      /* Release old first color */
      dlc_release(context, pdlc1);

      /* Update to point to new merged color */
      pdlc1->ce = ce;

      /* Cancel last colorant used in pdlc */
      pdlc1->ci = COLORANTINDEX_UNKNOWN;
      pdlc1->cv = COLORVALUE_INVALID;
    }
  }

  return fMergeOk;
}

Bool dl_merge_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*        pp_ncolor1,/* I/O */
  p_ncolor_t*        pp_ncolor2,/* I */
  dlc_merge_action_t action)    /* I */
{
  p_ncolor_t        p_ncolor1;
  p_ncolor_t        p_ncolor2;
  color_entry_t     ce;
  color_entry_t     ce1;
  color_entry_t     ce2;
  Bool              fMergeOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor1, "pp_ncolor1 is null");
  HQASSERT(pp_ncolor2, "pp_ncolor2 is null");

  p_ncolor1 = *pp_ncolor1;
  p_ncolor2 = *pp_ncolor2;

  HQASSERT(p_ncolor1, "p_ncolor1 is null");
  HQASSERT(p_ncolor2, "p_ncolor2 is null");

  ce_from_ncolor(&ce1, p_ncolor1);
  ce_from_ncolor(&ce2, p_ncolor2);

  if ( ce1.ppm[ 0 ] == PM_CMD ) {
    switch ( ce1.ppm[ 1 ] ) {
    case PM_CMD_ALL0 :
      if ( ce2.ppm[ 0 ] != PM_CMD || ce2.ppm[ 1 ] != PM_CMD_ALL0 ) {
        dl_release(context, pp_ncolor1) ;
        dlc_to_dl(context, pp_ncolor1, &context->dlc_black) ;
      }
      return TRUE ;
    case PM_CMD_ALL1 :
      return TRUE ;
    case PM_CMD_NONE :
      /* No need to do a release on dl none color. */
      return dl_copy(context, pp_ncolor1, pp_ncolor2) ;
    }
  }
  if ( ce2.ppm[ 0 ] == PM_CMD ) {
    switch ( ce2.ppm[ 1 ] ) {
    case PM_CMD_ALL0 :
    case PM_CMD_ALL1 :
      dl_release(context, pp_ncolor1) ;
      dlc_to_dl(context, pp_ncolor1, &context->dlc_black) ;
      return TRUE ;
    case PM_CMD_NONE :
      return TRUE ;
    }
  }

  if ( ce1.ppm[ 0 ] == PM_CMD ) {
    HQASSERT(ce1.ppm[ 1 ] == PM_CMD_NONE , "Should only be NONE command" ) ;
    /* No need to do a release on dl none color. */
    return dl_copy(context, pp_ncolor1, pp_ncolor2) ;
  }
  if ( ce2.ppm[ 0 ] == PM_CMD ) {
    HQASSERT(ce2.ppm[ 1 ] == PM_CMD_NONE , "Should only be NONE command" ) ;
    /* Nothing to do; already effectively been removed. */
    return TRUE ;
  }

  fMergeOk = ce_merge(context, &ce, &ce1, &ce2, action);
  HQASSERT(fMergeOk, "dl_merge_extra: ce_merge should not fail in this case");

  if ( fMergeOk ) {

    fMergeOk = dcc_update(context, &ce);

    if ( fMergeOk ) {
      /* Cache updated ok - return pointer to copy of color data */
      p_ncolor1 = (p_ncolor_t)ce.prc;

      /* Release old first color and update cc with new color */
      dl_release(context, pp_ncolor1);

      /* Return pointer to copy of color or NULL */
      *pp_ncolor1 = p_ncolor1;
    }
  }

  return fMergeOk;
}

/** dlc_merge_shfill is used for merging shfill colorants */
Bool dlc_merge_shfill_with_action(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  dl_color_t*        pdlc1,     /* I/O */
  dl_color_t*        pdlc2,     /* I */
  dlc_merge_action_t action)    /* I */
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc1, "pdlc1 is null");
  HQASSERT(pdlc2, "pdlc2 is null");

  /* In the max-blt case no COLORVALUE_TRANSPARENT are present, colors instead
     contain an overprint paint mask. This code assumes the two colorants have
     the same set of colorants (which is why it cannot be added to ce_merge). */
  if (pdlc1->ce.iop >= 0) {
    /* locate the overprint mask in the source color, and apply it to the target,
       taking into account any special cases where no colors overprint */
    paint_mask_t* ppm;
    int16 ipm;

    ppm = pm_from_dlc(pdlc2);

    HQASSERT((pm_locate_overprints(ppm, & ipm), ipm == pdlc2->ce.iop),
             "inconsistency between iop and pm overprint location");

    /* The same color entry implies the same intersection */
    if ( ppm == pm_from_dlc(pdlc1) )
      return TRUE ;

    ipm = pdlc2->ce.iop;

    if (ipm < 0) {
      if (! dlc_combine_overprints(context, NULL, DLC_INTERSECT_OP, pdlc1))
        return FALSE;
    } else {
      HQASSERT(ppm[ipm - 1] == PM_CMD_OPMAXBLT,
               "Overprint opcode should be maxblit") ;
      if (! dlc_combine_overprints(context, ppm + ipm, DLC_INTERSECT_OP, pdlc1))
        return FALSE;
    }
  }

  if ( !dlc_merge_with_action(context, pdlc1, pdlc2, action) )
    return FALSE;

  return TRUE ;
}

/** Return distance between two colours. Distance is not colorimetric, but is
    maximum of channel distances. */
COLORVALUE dlc_max_difference(const dl_color_t * pDlc_1,
                              const dl_color_t * pDlc_2)
{
  dl_color_iter_t dl_color_iter_1, dl_color_iter_2;
  dlc_iter_result_t iter_1, iter_2; /* DLC_ITER_ALLSEP, DLC_ITER_ALL01,
                                     DLC_ITER_COLORANT, DLC_ITER_NOMORE */
  COLORANTINDEX ci_1, ci_2;
  COLORVALUE cv_1, cv_2;
  int32 levels;
  COLORVALUE max_distance = 0;

  /* for all colorants... */
  for (iter_1 = dlc_first_colorant(pDlc_1, &dl_color_iter_1, &ci_1, &cv_1),
         iter_2 = dlc_first_colorant(pDlc_2, &dl_color_iter_2, &ci_2, &cv_2);
       iter_1 != DLC_ITER_NOMORE && iter_2 != DLC_ITER_NOMORE;
       iter_1 = dlc_next_colorant(pDlc_1, &dl_color_iter_1, &ci_1, &cv_1),
         iter_2 = dlc_next_colorant(pDlc_2, &dl_color_iter_2, &ci_2, &cv_2)) {
    HQASSERT(iter_1 == iter_2,
             "colorants walked differently; should have been transformed through same chain");
    HQASSERT(iter_1 != DLC_ITER_ALL01,
             "we can't cope with DLC_ITER_ALL01 - we don't know how many colorants there are");
    HQASSERT(iter_1 != DLC_ITER_NONE,
             "somehow we're are shading with NONE separation colors");
    HQASSERT(iter_1 == DLC_ITER_COLORANT || iter_1 == DLC_ITER_ALLSEP,
             "unexpected result from dlc_first/next_colorant");
    HQASSERT(ci_1 == ci_2, "different colorant indexes from dlc_first/next_colorant");

    levels = cv_1 - cv_2;

    if (levels < 0)
      levels = -levels;

    if ( levels > max_distance )
      max_distance = CAST_TO_COLORVALUE(levels) ;
  }

  HQASSERT(iter_1 == iter_2, "loop terminated with different conditions");
  return max_distance;
}

/** \brief Compares the given two colors to see if there are any colorants
 *         in common and returns up to 4 of them.
 *
 * \param[in] p_ncolor1    Pointer to first color to compare
 * \param[in] p_ncolor2    Pointer to second color to compare
 * \param[out] cis         Pointer to where to return colorants in common
 * \param[out] pnci        Pointer to where to return number of colorants in common
 *
 * \retval TRUE if successfully found less than 4 colorants in common
 * \retval FALSE if different colorants, or more than 4 in common
 *
 * Notes:
 */
Bool dl_upto_4_common_colorants(
         /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor1,
         /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor2,
         /*@notnull@*/ /*@out@*/ COLORANTINDEX cis[4],
         /*@notnull@*/ /*@out@*/ int32 *pnci)
{
  const paint_mask_t *ppm1 , *ppm2 ;
  paint_mask_t pm ;
  int32 ci , nci ;

  HQASSERT(p_ncolor1 != NULL , "dl_upto_4_common_colorants: p_ncolor1 null" ) ;
  HQASSERT(p_ncolor2 != NULL , "dl_upto_4_common_colorants: p_ncolor2 null" ) ;
  HQASSERT(cis != NULL , "dl_upto_4_common_colorants: cis null" ) ;
  HQASSERT(pnci != NULL , "dl_upto_4_common_colorants: pcipncinull" ) ;

  ppm1 = pm_from_ncolor( p_ncolor1 ) ;
  ppm2 = pm_from_ncolor( p_ncolor2 ) ;

  *pnci = 0 ;

  if ( ppm1[ 0 ] == PM_CMD || ppm2[ 0 ] == PM_CMD ) {
    /* One or both is none color */
    HQASSERT(ppm1[ 0 ] != PM_CMD || PM_IS_CMD_NONE( ppm1[ 1 ] ),
              "dl_upto_4_common_colorants: only cmd allowed for p_ncolor1 is none" ) ;
    HQASSERT(ppm2[ 0 ] != PM_CMD || PM_IS_CMD_NONE( ppm2[ 1 ] ),
              "dl_upto_4_common_colorants: only cmd allowed for p_ncolor2 is none" ) ;
    return TRUE ;
  }

  ci = 0 ;
  nci = 0 ;

  do {
    int32 bits ;
    paint_mask_t pm_mask ;

    pm = ( paint_mask_t ) ( *ppm1++ & *ppm2++ ) ;

    pm_mask = PM_GET_MASK( pm ) ;
    bits = count_bits_set_in_byte[ pm_mask ] ;

    if ( bits > 0 ) {
      if ( bits + nci > 4 )
        return FALSE ;
      do {
        int32 firstbitpos = highest_bit_set_in_byte[ pm_mask ] ;
        cis[ nci++ ] = ci + PM_COLORANT_BITS - firstbitpos - 1 ;
        pm_mask &= ~grgpmMask[ firstbitpos ] ;
      } while ((--bits) > 0 ) ;
    }
    ci += PM_COLORANT_BITS ;
  } while ( PM_CHAIN_SET( pm )) ;

  (*pnci) = nci ;

  return TRUE ;

} /* Function dl_upto_4_common_colorants */


/** \brief Creates a new color from the existing color wich ci colorant removed
 *
 * \param[in] context      DL color context.
 * \param[in,out] pp_ncolor_colors Pointer to color from which to take colorants and store new color
 * \param ci        Colorant to be excluded in new color
 *
 * \retval TRUE if succeeded
 * \retval FALSE if failed
 *
 * \note Colorant does not have to be in the dl color, it is inefficient
 * though.
 */
Bool dl_remove_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor_colors,
  COLORANTINDEX ci)
{
  p_ncolor_t        p_ncolor_colors;
  color_entry_t     ce;
  color_entry_t     ce_colors;
  Bool              fOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(ci >= 0, "Cannot remove special colorants") ;
  HQASSERT(pp_ncolor_colors != NULL, "dl_remove_colorant: NULL pointer to pp_ncolor_colors");
  p_ncolor_colors = *pp_ncolor_colors;
  HQASSERT(p_ncolor_colors != NULL, "dl_remove_colorant: NULL pointer to p_ncolor_colors");

  ce_from_ncolor(&ce_colors, p_ncolor_colors);

  if ( ce_colors.ppm[ 0 ] == PM_CMD ) {
    HQASSERT(ce_colors.ppm[ 1 ] == PM_CMD_NONE , "Should only be NONE command" ) ;
    /* Nothing to do; already effectively been removed. */
    return TRUE ;
  }

  if ( ce_colors.ccv == 1 ) {
    int32 icv ;
    if ( pm_colorant_offset( ce_colors.ppm , ci , & icv )) {
      /* Release old first color and update cc with None color */
      dl_release(context, pp_ncolor_colors);
      *pp_ncolor_colors = (p_ncolor_t)context->dlc_none.ce.prc;
    }
    return TRUE ;
  }

  ce_remove_colorant(context, &ce, ci, &ce_colors);

  fOk = dcc_update(context, &ce);

  if ( fOk ) {
    /* Cache updated ok - return pointer to copy of color data */
    p_ncolor_colors = (p_ncolor_t)ce.prc;

    /* Release old first color and update cc with new color */
    dl_release(context, pp_ncolor_colors);

    /* Return pointer to copy of color or NULL */
    *pp_ncolor_colors = p_ncolor_colors;
  }

  return fOk;

} /* Function dl_remove_colorant */


/** \brief Create a new color from the existing color wich ci colorant removed
 *
 * \param pdlc        Pointer to color from which to take colorants
 *                    and store new color
 * \param ci          Colorant to be excluded in new color
 *
 * \retval TRUE if successful
 * \retval FALSE if failed
 */
Bool dlc_remove_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t *pdlc,
  COLORANTINDEX ci)
{
  color_entry_t     ce;
  Bool              fOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(ci >= 0, "Cannot remove special colorants") ;
  HQASSERT(pdlc != NULL, "dlc_remove_colorant: NULL pointer to dlc");

  if ( pdlc->ce.ppm[ 0 ] == PM_CMD ) {
    HQASSERT(pdlc->ce.ppm[ 1 ] == PM_CMD_NONE , "Should only be NONE command" ) ;
    /* Nothing to do; already effectively been removed. */
    return TRUE ;
  }

  if ( pdlc->ce.ccv == 1 ) {
    int32 icv ;
    if ( pm_colorant_offset( pdlc->ce.ppm , ci , & icv )) {
      /* Release old first color and update cc with None color */
      dlc_release(context,  pdlc ) ;
      *pdlc = context->dlc_none ;
    }
    return TRUE ;
  }

  ce_remove_colorant(context, &ce, ci, &pdlc->ce);

  fOk = dcc_update(context, &ce);

  if ( fOk ) {
    /* Release old first color */
    dlc_release(context, pdlc);

    /* Update to point to new merged color */
    pdlc->ce = ce;

    /* Cancel last colorant used in pdlc */
    pdlc->ci = COLORANTINDEX_UNKNOWN;
  }

  return fOk;

} /* Function dlc_remove_colorant */


/** \brief Checks if two colors have colorants in common.
 *
 * \param p_ncolor1     Pointer to first color
 * \param p_ncolor2     Pointer to second color
 *
 * \retval TRUE if colorants in common
 * \retval FALSE if no intersection
 */
Bool dl_common_colorants(
  /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor1 ,
  /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor2 )
{
  paint_mask_t pm ;
  paint_mask_t *ppm1 , *ppm2 ;

  HQASSERT(p_ncolor1 != NULL , "dl_common_colorants: p_ncolor1 NULL" ) ;
  HQASSERT(p_ncolor2 != NULL , "dl_common_colorants: p_ncolor2 NULL" ) ;

  ppm1 = pm_from_ncolor( p_ncolor1 ) ;
  ppm2 = pm_from_ncolor( p_ncolor2 ) ;

  if ( ppm1[0] == PM_CMD || ppm2[0] == PM_CMD )
    /* One or both is a special - intersect unless one is a none */
    return ( (ppm1[0] != PM_CMD || !PM_IS_CMD_NONE(ppm1[1])) &&
             (ppm2[0] != PM_CMD || !PM_IS_CMD_NONE(ppm2[1])) );

  /* Neither none - must explicitly compare paintmasks */
  do {
    pm = ( paint_mask_t ) ( *ppm1++ & *ppm2++ ) ;
    if ( PM_GET_MASK( pm ) != 0 )
      return TRUE ;
  } while ( PM_CHAIN_SET( pm )) ;

  /* ... taking no account of /All or overprinting commands */

  return FALSE;

} /* Function dl_common_colorants */


/** \brief Checks if two colors have colorants in common.
 *
 * \param[in] pdlc1     Pointer to first color
 * \param[in] pdlc2     Pointer to second color
 *
 * \retval TRUE if colorants in common
 * \retval FALSE if no intersection
 */
Bool dlc_common_colorants(
  /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc1 ,
  /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc2 )
{
  paint_mask_t pm ;
  paint_mask_t *ppm1 , *ppm2 ;

  HQASSERT(pdlc1 != NULL , "dlc_common_colorants: pdlc1 NULL" ) ;
  HQASSERT(pdlc2 != NULL , "dlc_common_colorants: pdlc2 NULL" ) ;

  if ( pdlc1 == pdlc2 ||
       pdlc1->ce.prc == pdlc2->ce.prc )
    return TRUE ;

  ppm1 = pm_from_dlc( pdlc1 ) ;
  ppm2 = pm_from_dlc( pdlc2 ) ;

  if ( ppm1[0] == PM_CMD || ppm2[0] == PM_CMD )
    /* One or both is a special - intersect unless one is a none */
    return ( (ppm1[0] != PM_CMD || !PM_IS_CMD_NONE(ppm1[1])) &&
             (ppm2[0] != PM_CMD || !PM_IS_CMD_NONE(ppm2[1])) );

  /* Neither none - must explicitly compare paintmasks */
  do {
    pm = ( paint_mask_t ) ( *ppm1++ & *ppm2++ ) ;
    if ( PM_GET_MASK( pm ) != 0 )
      return TRUE ;
  } while ( PM_CHAIN_SET( pm )) ;

  /* ... taking no account of /All or overprinting commands */

  return FALSE;

} /* Function dlc_common_colorants */


/** \brief Checks if the second color contains colorant not present
 * in the first color.
 *
 * \param pdlc1     Pointer to first color
 * \param pdlc2     Pointer to second color
 *
 * \retval TRUE if second color has extra colorants
 * \retval FALSE if second color has no extra colorants
 */
Bool dlc_extra_colorants(
  const dl_color_t*   pdlc1,      /* I */
  const dl_color_t*   pdlc2)      /* I */
{
  paint_mask_t*     ppm1;
  paint_mask_t*     ppm2;
  paint_mask_t      pm1;
  paint_mask_t      pm2;

  HQASSERT(pdlc1 != NULL, "dl_extra_colorants: pdlc1 null");
  HQASSERT(pdlc2 != NULL, "dl_extra_colorants: pdlc2 null");

  ppm1 = pm_from_dlc(pdlc1);
  ppm2 = pm_from_dlc(pdlc2);

  HQASSERT(ppm1 != NULL, "dl_extra_colorants: ppm1 null");
  HQASSERT(ppm2 != NULL, "dl_extra_colorants: ppm2 null");

  if ( ppm1[0] == PM_CMD || ppm2[0] == PM_CMD ) {
    /* One or both is a special
       - extra iff first is none and second not none */
    return ( (ppm1[0] == PM_CMD && PM_IS_CMD_NONE(ppm1[1])) &&
             (ppm2[0] != PM_CMD || !PM_IS_CMD_NONE(ppm2[1])) );
  }
  else {
    /* Neither special - must explicitly compare paintmasks */
    do {
      paint_mask_t mask1, mask2;
      pm1 = *ppm1++;
      pm2 = *ppm2++;
      /* mask1, mask2 exclude the mask chain bit */
      mask1 = PM_GET_MASK(pm1);
      mask2 = PM_GET_MASK(pm2);
      if ((mask1 | mask2) != mask1)
        return TRUE;
    } while (PM_CHAIN_SET(pm1) && PM_CHAIN_SET(pm2));

    /* There are extra colorants when one mask continues; but exclude
       max blit flags etc (which follow the normal mask with PM_CMD) */
    if (PM_CHAIN_SET(pm2) && *ppm2 != PM_CMD)
      return TRUE;
  }

  /* ... taking no account of /All or overprinting commands */

  return FALSE;

} /* Function dlc_extra_colorants */


/** \brief Find if at least one one of the given colorants is present in
 * the dl color entry.
 *
 * \param[in] pdlc        Pointer to dl color entry to check
 * \param[in] rgci        Array of colorant indexes to check for
 *
 * \retval TRUE if one of the given colorants is present in the dl color entry
 * \retval FALSE if none of the colorats are present in the dl color entry
 *
 * The array of colorant indexes is limited to 4 values with a terminating
 * index of COLORANTINDEX_UNKNOWN for fewer values. Yuck: why? \c
 * dlc_indexed_colorant_present() is an alternative version but only handles
 * a single colorant.
 *
 */
Bool dlc_some_colorant_exists(
       /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc,
       /*@notnull@*/ /*@in@*/ const COLORANTINDEX rgci[])
{
  paint_mask_t *pm;
  int32        ici;
  int32        ipm;
  int32        ipmT;
  int32        bit_index;
  Bool         fColorantFound = FALSE;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdlc->ce.prc != NULL, "NULL pointer to dl color entry paint mask");
  HQASSERT(rgci != NULL, "NULL pointer to array of indexes to check");

  pm = pm_from_dlc(pdlc);

  if ( *pm != PM_CMD ) {
    /* General paint mask - look for first colorant present */

    for ( ici = 0; !fColorantFound && (ici < 4); ici++ ) {
      if (rgci[ici] != COLORANTINDEX_UNKNOWN) {
        HQASSERT(rgci[ici] >= 0, "Special colorant indices not permitted") ;

        /* Scan to the required paint mask */
        ipm = PM_INDEX(rgci[ici]);
        for ( ipmT = 0; (ipmT < ipm) && PM_CHAIN_SET(pm[ipmT]); ipmT++ ) {
          EMPTY_STATEMENT();
        }

        if ( ipmT == ipm && pm[ipm] != PM_CMD ) {
          /* Found paint mask - check for bit set */
          bit_index = PM_BIT_INDEX(rgci[ici]);
          fColorantFound = PM_COLORANT_SET(pm[ipm], bit_index);
        }
      }
    }

  } else { /* All colorants exist for special case black or white dl color entries */
    fColorantFound = PM_IS_CMD_ALL0(pm[1]) || PM_IS_CMD_ALL1(pm[1]);
  }

  return fColorantFound;

} /* Function dlc_some_colorant_exists */

Bool dlc_indexed_colorant_present(const dl_color_t* color, COLORANTINDEX ci)
{
  COLORANTINDEX cis[2];

  cis[0] = ci;
  cis[1] = COLORANTINDEX_UNKNOWN;

  return dlc_some_colorant_exists(color, cis);
}

/** \brief Get colorant value for given colorant index.
 *
 * \param[in] cpdlc            Pointer to dl color entry
 * \param ci                   Required colorant index
 * \param[out] pcv             Pointer to returned colorvalue
 *
 * \retval TRUE if colorant exists
 * \retval FALSE if colorant does not exist.
 *
 * The special colorant indices \c COLORANTINDEX_ALL and \c COLORANTINDEX_ALPHA
 * may be used to enquire and return the All separation or opacity channel
 * of the color. If an All separation exists, the color will always be noted
 * as existing, and the All separation colorvalue will be returned if no
 * colorant is found.
 */
Bool dlc_get_indexed_colorant_(
        /*@notnull@*/ /*@in@*/ const dl_color_t* cpdlc,
                               COLORANTINDEX ci,
       /*@notnull@*/ /*@out@*/ COLORVALUE *pcv)
{
  dl_color_t *pdlc = (dl_color_t *)cpdlc ; /* Cached index and cv are not const */
  Bool    fExists;
  int32   icv;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color");
  HQASSERT(ci >= 0 || ci == COLORANTINDEX_ALL || ci == COLORANTINDEX_ALPHA,
           "-ve colorant index");
  HQASSERT(pcv != NULL, "NULL pointer to returned color value");
  HQASSERT(pdlc->ci != ci, "index already in cache");

  /* Try and get color value offset for colorant */
  fExists = pm_colorant_offset(pm_from_dlc(pdlc), ci, &icv);

  if ( fExists ) {
    /* Valid colorant index for this dl color - update last colorant accessed */
    pdlc->ci = ci;
    pdlc->cv = pdlc->ce.pcv[icv];

    /* Return colorant value */
    *pcv = pdlc->cv;
  }

  return fExists;

} /* Function dlc_get_indexed_colorant_ */


/** \brief Replaces a single colorant value for given colorant index
 *
 * \param pdlc            Pointer to dl color entry
 * \param ci              Required colorant index
 * \param cv              Colorvalue to replace
 *
 * \retval TRUE if colorant exists and was replaced
 * \retval FALSE if the colorant could not be replaced.
 */
Bool dlc_replace_indexed_colorant(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ dl_color_t *pdlc,
  COLORANTINDEX   ci,         /* I */
  COLORVALUE      cv)         /* I */
{
  int32 icv ;
  Bool fExists ;
  color_entry_t ce ;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(ci >= 0, "-ve colorant index");

  fExists = pm_colorant_offset( pdlc->ce.ppm , ci , & icv ) ;
  HQASSERT(fExists , "Can only replace existing colorants" ) ;
  if ( pdlc->ce.pcv[ icv ] == cv )
    return TRUE ;

  ce_build_template(context, &ce, &pdlc->ce) ;
  HqMemCpy( ce.pcv , pdlc->ce.pcv , pdlc->ce.ccv * sizeof( COLORVALUE )) ;
  ce.pcv[ icv ] = cv ;

  if ( !dcc_update(context, &ce) )
    return FALSE ;

  dlc_release(context, pdlc) ;

  pdlc->ce = ce ;
  pdlc->ci = COLORANTINDEX_UNKNOWN ;

  return TRUE ;
}


/** \brief Try to set up caching state for given colorant index.
 *
 * \param cpdlc           Pointer to dl color entry
 * \param ci              Required colorant index
 *
 * \retval TRUE if colorant exists
 * \retval FALSE if colorant does not exist.
 *
 * \note Last colorant accessed vars only updated IFF the colorant index is
 * valid. Except for special case black and white dl color entries - since by
 * definition all colorants exist in the dl color entry but we do reset the
 * cached values in case any smart Alecs try to get the indexed color value -
 * should have done a black/white check instead.
 */
Bool dlc_set_indexed_colorant(
       /*@notnull@*/ /*@in@*/ const dl_color_t *cpdlc,
                              COLORANTINDEX ci)
{
  dl_color_t *pdlc = (dl_color_t *)cpdlc ; /* Cached index and cv are not const */
  paint_mask_t *pm;
  Bool         fExists;
  int32        icv;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color");
  HQASSERT(ci >= 0 || ci == COLORANTINDEX_ALL, "-ve colorant index");

  pm = pm_from_dlc(pdlc);

  if ( *pm != PM_CMD ) {
    /* General case - see if colorant exists in paint mask */
    fExists = pm_colorant_offset(pm, ci, &icv);

    if ( fExists ) {
      /* Valid colorant index for this dl color - update last colorant accessed vars */
      pdlc->ci = ci;
      pdlc->cv = pdlc->ce.pcv[icv];
    }

  } else { /* Fake that colorant exists for black and white special cases */
    fExists = PM_IS_CMD_ALL0(pm[1]) || PM_IS_CMD_ALL1(pm[1]);

    if ( fExists ) {
      /* Reset cache as could match index and get erroneous color value */
      pdlc->ci = COLORANTINDEX_UNKNOWN;
    }
  }

  return fExists;

} /* Function dlc_set_indexed_colorant */

/** Helper function for \c dl_remap_colorants, to build a new paintmask
    with the colorant bits remapped.

    \param[in] ippm      The original paintmask that is being remapped.
    \param[out] isize    The number of bytes read from the original paintmask.
    \param[out] oppm     The re-mapped paintmask.
    \param[out] osize    The size in bytes of the re-mapped paintmask.
    \param[out] ccv      The number of unique colorants
    \param[in] map       The colorant index mapping, from pseudo to actual
                         colorant indices.
    \param maplength     The number of entries in the colorant map.

    \retval TRUE If the new paintmask was different from the old one.
    \retval FALSE If the new paintmask was identical to the old one (no
    remapping was necessary).
*/
static inline Bool pm_remap_colorants(/*@notnull@*/ /*@in@*/  const paint_mask_t ippm[],
                                      /*@notnull@*/ /*@out@*/ int32 *isize,
                                      /*@notnull@*/ /*@out@*/ paint_mask_t oppm[],
                                      /*@notnull@*/ /*@out@*/ int32 *osize,
                                                    /*@out@*/ int16 *ccv,
                                      /*@notnull@*/ /*@in@*/  const COLORANTINDEX *map,
                                      int32 maplength)
{
  Bool changed = FALSE ;
  COLORANTINDEX ci ;
  int32 pi, npsize ;

  /* We know there will be at least one colorant, so initialise the first
     paintmask byte. This allows us to extend the new paintmask by ORing
     PM_CHAIN onto the last byte, without testing whether it is the first
     byte. */
  oppm[0] = 0 ;
  npsize = 1 ; /* New paintmask size */
  if ( ccv != NULL ) *ccv = 0 ;

  /* Build a new paintmask by re-mapping the colorant indices. Keep track if
     there were any changes during this process; if there were not, we can
     exit without completing the new colour. */
  pi = 0 ;
  ci = 0 ;
  do {
    if ( ippm[pi] == PM_CHAIN ) { /* Nothing set in this PM byte */
      ci += PM_COLORANT_BITS ;
    } else {
      uint8 mask = PM_FIRST_COLORANT ;

      do {
        if ( (ippm[pi] & mask) != 0 ) {
          COLORANTINDEX nci = ci ;
          int32 npi ; /* New colorant's paintmask index */

          if ( ci < maplength )
            nci = map[ci] ; /* Get remapped colorant. */

          if ( ci != nci ) /* Is mapped colorant same index? */
            changed = TRUE ;

          npi = PM_INDEX(nci) ;
          while ( npi >= npsize ) { /* Extend new paintmask */
            oppm[npsize - 1] |= PM_CHAIN ;
            oppm[npsize++] = 0 ;
          }

          if ( (oppm[npi] & grgpmMask[PM_BIT_INDEX(nci)]) == 0 ) {
            oppm[npi] |= grgpmMask[PM_BIT_INDEX(nci)] ;
            if ( ccv != NULL ) ++(*ccv) ;
          }
        }
        ++ci ;
        mask >>= 1 ;
      } while ( mask != 0 ) ;
    }

    ++pi ;
  } while ( PM_CHAIN_SET(ippm[pi - 1]) && ippm[pi] != PM_CMD ) ;

  *isize = pi ;
  *osize = npsize ;

  return changed ;
}

Bool dl_remap_colorants(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t incolor,
  p_ncolor_t *oncolor,
  const COLORANTINDEX *map,
  int32 maplength)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(incolor, "No input ncolor") ;
  HQASSERT(oncolor, "Nowhere to put output ncolor") ;
  HQASSERT(map, "No colorant map") ;
  HQASSERT(maplength >= 0, "Colorant map has invalid length") ;

  if ( maplength > 0 ) {
    color_entry_t ce ;

    /* Lightweight test at first, check if the paintmask is a special command
       before we unpack the whole thing. */
    ce.ppm = pm_from_ncolor(incolor) ;

    if ( ce.ppm[0] != PM_CMD ) {
      /* Iterate over colorants in colour, building a new color entry with
         the mapped colorants. */
      color_entry_t ce_new ;
      int32 pi, po ; /* Paintmask indices */

      /* Get the old color entry to iterate */
      ce_from_ncolor(&ce, incolor) ;

      /* Prepare a new color entry */
      debug_scribble_buffer(context, ce.ccv);
      ce_new.prc = (ref_count_t *)&context->grgbColorBuffer[0] ;
      *ce_new.prc = (ref_count_t)1 ;
      ce_new.ppm = (paint_mask_t *)(ce_new.prc + 1) ;
      ce_new.pcv = NULL ; /* We don't yet know this */
      ce_new.ccv = ce.ccv ; /* May be overridden by pm_remap_colorants */
      ce_new.iop = -1 ; /* We need to know the mapping before deciding this. */
      ce_new.allsep = ce.allsep ;
      ce_new.opacity = ce.opacity ;

      if ( pm_remap_colorants(ce.ppm, &pi, ce_new.ppm, &po, &ce_new.ccv,
                              map, maplength) ) {
        Bool has_all = FALSE, has_opacity = FALSE ;
        int32 cvi ;
        COLORANTINDEX ci ;

        /* Colorants have changed; add special commands to paintmasks, and
           continue building new color. */
        if ( PM_CHAIN_SET(ce.ppm[pi - 1]) &&
             ce.ppm[pi] == PM_CMD &&
             PM_IS_CMD_ALLSEP(ce.ppm[pi + 1]) ) {
          HQASSERT(ce.ppm[pi] == PM_CMD, "/All separation command incorrect") ;
          ce_new.ppm[po - 1] |= PM_CHAIN ;
          ce_new.ppm[po++] = PM_CMD ;
          ce_new.ppm[po++] = PM_CMD_ALLSEP ;
          pi += 2 ;
          has_all = TRUE ;
        }

        if ( PM_CHAIN_SET(ce.ppm[pi - 1]) &&
             ce.ppm[pi] == PM_CMD &&
             PM_IS_CMD_OPACITY(ce.ppm[pi + 1]) ) {
          HQASSERT(ce.ppm[pi] == PM_CMD, "Opacity command incorrect") ;
          ce_new.ppm[po - 1] |= PM_CHAIN ;
          ce_new.ppm[po++] = PM_CMD ;
          ce_new.ppm[po++] = PM_CMD_OPACITY ;
          pi += 2 ;
          has_opacity = TRUE ;
        }

        if ( PM_CHAIN_SET(ce.ppm[pi - 1]) &&
             ce.ppm[pi] == PM_CMD &&
             PM_IS_CMD_OPMAXBLT(ce.ppm[pi + 1]) ) {
          int32 dummy ;
          HQASSERT(ce.ppm[pi] == PM_CMD, "Maxblit command incorrect") ;
          ce_new.ppm[po - 1] |= PM_CHAIN ;
          ce_new.ppm[po++] = PM_CMD ;
          ce_new.ppm[po++] = PM_CMD_OPMAXBLT ;
          pi += 2 ;
          ce_new.iop = CAST_SIGNED_TO_INT16(po) ; /* Save the start index for overprint mask */
          /* Remap the maxblit paintmask; we don't care if it was changed or
             not, it should be a strict subset of the channels in the
             main paintmask. */
          (void)pm_remap_colorants(&ce.ppm[pi], &dummy, &ce_new.ppm[po], &po,
                                   NULL, map, maplength) ;
          po += ce_new.iop ;
        }

        /* Align up for the colorvalue storage and initialise values to
           transparent in case need to merge values from duplicate colorants. */
        ce_new.pcv = (COLORVALUE*)((uint8*)ce_new.prc + DLC_HEADERSIZE(po));
        for ( po = 0; po < ce_new.ccv; ++po )
          ce_new.pcv[po] = COLORVALUE_TRANSPARENT;

        /* We now need to copy the colorvalues across. We know how many there
           are, however the order may have changed. Walk over the input paint
           mask; when we see colorant, work out the mapping, and the position
           in the output colorant list, and store it there. If /All and/or
           opacity were used, add them at the end. */
        pi = cvi = 0 ;
        ci = 0 ;
        do {
          if ( ce.ppm[pi] == PM_CHAIN ) { /* Nothing set in this PM byte */
            ci += PM_COLORANT_BITS ;
          } else {
            uint8 mask = PM_FIRST_COLORANT ;

            do {
              if ( (ce.ppm[pi] & mask) != 0 ) {
                COLORANTINDEX nci = ci ;

                if ( ci < maplength )
                  nci = map[ci] ; /* Get remapped colorant. */

                if ( !pm_colorant_offset(ce_new.ppm, nci, &po) )
                  HQFAIL("Remapped colorant should exist in new paintmask") ;
                HQASSERT(po < ce_new.ccv, "bad colorant offset");

                ce_merge_values(ce_new.pcv[po], ce.pcv[cvi++],
                                COMMON_COLORANT_AVERAGE, &ce_new.pcv[po]);
              }
              ++ci ;
              mask >>= 1 ;
            } while ( mask != 0 ) ;
          }

          ++pi ;
        } while ( PM_CHAIN_SET(ce.ppm[pi - 1]) && ce.ppm[pi] != PM_CMD ) ;

        if ( has_all ) {
          if ( !pm_colorant_offset(ce_new.ppm, COLORANTINDEX_ALL, &po) )
            HQFAIL("Remapped All colorant should exist in new paintmask") ;
          ce_new.pcv[po] = ce.pcv[cvi++] ;
        }

        if ( has_opacity ) {
          if ( !pm_colorant_offset(ce_new.ppm, COLORANTINDEX_ALPHA, &po) )
            HQFAIL("Remapped opacity should exist in new paintmask") ;
          ce_new.pcv[po] = ce.pcv[cvi++] ;
        }

        /* Hash the modified color and store it back in the color store. */
        if ( !dcc_update(context, &ce_new) )
          return FALSE ;

        /* Lose a reference to the original colour, replace with the
           newly-hashed colour, which will be returned on exit. */
        HQASSERT(ce.prc != ce_new.prc,
                 "Changed color entry is same as original color entry") ;
        dl_release(context, &incolor) ;
        incolor = (p_ncolor_t)ce_new.prc ;
      }
    }
  }

  *oncolor = incolor ;

  return TRUE ;
}

/* ------------- Public listobject n color functions --------------- */

/** \brief Construct wrapper color struct from dl color object.
 *
 * \param[in] pp_ncolor Pointer to pointer n color entry in dl object or elsewhere
 * \param[out] pdlc     Pointer to dl color struct to fill in
 *
 * \retval TRUE if wrapper constructed ok
 * \retval FALSE if failed
 *
 * \note The original n color pointer pp_ncolor may change if the
 * ref count maxes out!
 */
Bool dlc_from_dl(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ p_ncolor_t *pp_ncolor,
  /*@notnull@*/  dl_color_t *pdlc)
{
  Bool        fOk;
  p_ncolor_t  p_ncolorCopy;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor != NULL, "NULL pointer to pointer to n color");
  HQASSERT(*pp_ncolor != NULL, "NULL pointer to n color");
  HQASSERT(pdlc != NULL, "NULL pointer to returned dl color");

  /* Get ref counted copy of color data pointer */
  fOk = dl_copy(context, &p_ncolorCopy, pp_ncolor);

  if ( fOk ) {
    /* Got copy of color data pointer - build up wrapper struct */
    ce_from_ncolor(&pdlc->ce, p_ncolorCopy);

    /* cancel last colorant used in pdlc */
    pdlc->ci = COLORANTINDEX_UNKNOWN;
  }

  return fOk;

} /* Function dlc_from_dl */


/** \brief Construct wrapper color struct from dl color object.
 *
 * \param p_ncolor        Pointer to n color entry in dl object or elsewhere
 * \param pdlc            Pointer to dl color struct to fill in
 */
void dlc_from_dl_weak(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor,
 /*@notnull@*/ /*@in@*/ dl_color_t *pdlc)
{
  HQASSERT(p_ncolor != NULL,
           "dlc_from_dl_object: NULL pointer to ncolor");
  HQASSERT(pdlc != NULL,
           "dlc_from_dl_object: NULL pointer to returned dl color");

  /* Setup color entry from dl color pointer */
  ce_from_ncolor(&(pdlc->ce), p_ncolor);

  /* cancel last colorant used in pdlc */
  pdlc->ci = COLORANTINDEX_UNKNOWN;

} /* Function dlc_from_dl_weak */


/** \brief Build a dl wrapper struct from a dl object
 *
 * \param p_lobj      Pointer to dl object
 * \param pdlc        Pointer to filled in dl object
 *
 * \retval TRUE if filled in wrapper ok
 * \retval FALSE if failed
 */
Bool dlc_from_lobj(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  LISTOBJECT*   p_lobj,     /* I */
  dl_color_t*   pdlc)       /* O */
{
  Bool fOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(p_lobj != NULL, "NULL pointer to list object");
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  /* Set up wrapper from color data pointer */
  fOk = dlc_from_dl(context, &p_lobj->p_ncolor, pdlc);

  return fOk;

} /* Function dlc_from_lobj */


/** \brief To set an n color pointer from a dl color entry.
 *
 * \param pp_ncolor   Pointer to n color pointer to get copy of n color pointer
 * \param pdlc        Pointer to dl color entry to copy from
 *
 * \retval TRUE if copied pointer to n color pointer with re counting ok
 * \retval FALSE if failed
 */
Bool dlc_to_dl(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*   pp_ncolor,  /* I/O */
  dl_color_t*   pdlc)       /* I */
{
  Bool        fOk;
  p_ncolor_t  p_ncolorT;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  /* See if we can handle another reference to the color */
  p_ncolorT = (p_ncolor_t)(pdlc->ce.prc);
  fOk = dl_copy(context, pp_ncolor, &p_ncolorT);

  if ( fOk && (p_ncolorT != (p_ncolor_t)(pdlc->ce.prc)) ) {
    /* Original reference changed under our feet - rebuild color entry */
    ce_from_ncolor(&(pdlc->ce), p_ncolorT);
  }

  return fOk;

} /* Function dlc_to_dl */


/** \brief To set a list object color entry from a dl color entry.
 *
 * \param p_lobj      Pointer to dl object
 * \param pdlc        Pointer to dl color entry to copy from
 *
 * \retval TRUE if added to dl ok
 * \retval FALSE if failed
 */
Bool dlc_to_lobj(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  LISTOBJECT*     p_lobj,     /* I/O */
  dl_color_t*     pdlc)       /* I */
{
  Bool fOk;

  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(p_lobj != NULL, "NULL pointer to dl object");
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  /* Copy the n color pointer to the list object */
  fOk = dlc_to_dl(context, &p_lobj->p_ncolor, pdlc);

  if ( fOk ) {
    if (pdlc->ce.opacity < COLORVALUE_ONE)
      p_lobj->marker |= (MARKER_TRANSPARENT | MARKER_OMNITRANSPARENT);
  }

  return fOk;

} /* Function dlc_to_lobj */


/** \brief To set dl object color entry from dl color entry and release
 * original reference.
 *
 * \param p_lobj      Pointer to dl object
 * \param pdlc        Pointer to dl color entry to copy from
 */
void dlc_to_lobj_release(
  LISTOBJECT*   p_lobj,     /* I/O */
  dl_color_t*   pdlc)       /* I */
{
  HQASSERT(p_lobj != NULL, "NULL pointer to dl object");
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  /* Create a dl color entry from dl object and copy contents */
  dlc_to_dl_weak(&(p_lobj->p_ncolor), pdlc);

  if (pdlc->ce.opacity < COLORVALUE_ONE)
    p_lobj->marker |= (MARKER_TRANSPARENT | MARKER_OMNITRANSPARENT);

  /* Clear out wrapper reference */
  dlc_clear(pdlc);

} /* Function dlc_to_lobj_release */

/*----------------------------------------------------------------------------*/
/**
 * Make a dl color containing the full set of fully-fledged colorants from the
 * raster style with all the values set to cvInit.
 */
Bool dlc_from_rs(DL_STATE *page, GUCR_RASTERSTYLE *rs,
                 dl_color_t *dlc, COLORVALUE cvInit)
{
  Bool result = FALSE;
  COLORANTINDEX *colorants;
  COLORVALUE *values;
  uint32 nColorants, nUnique, i;

  gucr_colorantCount(rs, &nColorants);

  colorants = mm_alloc(mm_pool_temp, nColorants * sizeof(COLORANTINDEX),
                       MM_ALLOC_CLASS_RCB_ADJUST);
  values = mm_alloc(mm_pool_temp, nColorants * sizeof(COLORVALUE),
                    MM_ALLOC_CLASS_RCB_ADJUST);
  if ( colorants == NULL || values == NULL ) {
    (void)error_handler(VMERROR);
    goto cleanup;
  }

  gucr_colorantIndices(rs, colorants, &nUnique);
  HQASSERT(nUnique <= nColorants, "nUnique cannot be greater than nColorants");
  for ( i = 0; i < nUnique; ++i )
    values[i] = cvInit;

  if ( !dlc_alloc_fillin(page->dlc_context, nUnique, colorants, values, dlc) )
    goto cleanup;

  result = TRUE;
 cleanup:
  if ( colorants != NULL )
    mm_free(mm_pool_temp, colorants, nColorants * sizeof(COLORANTINDEX));
  if ( values != NULL )
    mm_free(mm_pool_temp, values, nColorants * sizeof(COLORVALUE));
  return result;
}

dlc_tint_t dlc_check_black_white(const dl_color_t *pdlc)
{
  dlc_tint_t  tint;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  /* Check for special paint mask handling */
  switch ( pm_special_type(pm_from_dlc(pdlc)) ) {
  case PM_BLACK:
    tint = DLC_TINT_BLACK;
    break;

  case PM_WHITE:
    tint = DLC_TINT_WHITE;
    break;

  default:
    tint = DLC_TINT_OTHER;
    break;
  }

  return tint;

} /* Function dlc_check_black_white */


/** \brief To test for /All sep command code in dl color entry, or a special
 * All black or All white command.
 *
 * \param[in] pdlc         Pointer to dl color entry
 *
 * \retval TRUE if dl color entry covers all separations
 * \retval FALSE if no /All separation
 */
Bool dlc_has_allsep(
 /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc)
{
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  return pdlc->ce.allsep != COLORVALUE_TRANSPARENT ;
} /* Function dlc_has_allsep */


/** \brief Find first colorant for dl color entry.
 *
 * \param[in] pdlc        Pointer to dl color entry
 * \param[out] pdci       Pointer to dl color iterator
 * \param[out] pci        Pointer to returned colorant index
 * \param[out] pcv        Pointer to returned colorant value
 *
 * \retval DLC_ITER_NOMORE if no more colorants in paint mask
 * \retval DLC_ITER_COLORANT if found colorant bit
 * \retval DLC_ITER_ALLSEP if found all separation wild card match
 * \retval DLC_ITER_NONE if found none command code.
 * \retval DLC_ITER_ALL01 if found either all0 or all1 command codes.
 */
int32 dlc_first_colorant(
  /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc,
 /*@notnull@*/ /*@out@*/ dl_color_iter_t *pdci,
 /*@notnull@*/ /*@out@*/ COLORANTINDEX *pci,
 /*@notnull@*/ /*@out@*/ COLORVALUE *pcv)
{
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdci != NULL, "NULL pointer to dl color iterator");
  HQASSERT(pci != NULL, "NULL pointer to returned colorant index");
  HQASSERT(pcv != NULL, "NULL pointer to returned colorant value");

  /* Reset iteration state */
  pdci->ppmLast = pm_from_dlc(pdlc);
  pdci->pmLast  = PM_FIRST_COLORANT << 1;
  pdci->ciLast  = -1;
  pdci->pcvLast = pdlc->ce.pcv - 1;

  /* Find first colorant */
  return dlc_next_colorant(pdlc, pdci, pci, pcv);

} /* Function dlc_first_colorant*/


/** \brief To find the next colorant in the dl color entry.
 *
 * \param[in] cpdlc       Pointer to dl color entry
 * \param[in,out] pdci    Pointer to dl color iterator
 * \param[out] pci        Pointer to returned colorant index
 * \param[out] pcv        Pointer to returned colorant value
 *
 * \retval DLC_ITER_NOMORE if no more colorants in paint mask
 * \retval DLC_ITER_COLORANT if found colorant bit
 * \retval DLC_ITER_ALLSEP if found all separation wild card match
 * \retval DLC_ITER_NONE if found none command code.
 * \retval DLC_ITER_ALL01 if found either all0 or all1 command codes.
 */
int32 dlc_next_colorant(
 /*@notnull@*/ /*@in@*/ const dl_color_t *cpdlc,
          /*@notnull@*/ dl_color_iter_t *pdci,
/*@notnull@*/ /*@out@*/ COLORANTINDEX *pci,
/*@notnull@*/ /*@out@*/ COLORVALUE *pcv)
{
  dl_color_t *pdlc = (dl_color_t *)cpdlc ; /* Cached index and cv are not const */
  COLORANTINDEX   ci;
  Bool            fGotColorant = FALSE;
  paint_mask_t    pm;
  paint_mask_t    pmCheck;
  paint_mask_t*   ppm;

  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdci != NULL, "NULL pointer to dl color iterator");
  HQASSERT(pdci->ppmLast != NULL, "NULL pointer last paint mask checked");
  HQASSERT(pdci->pcvLast != NULL, "NULL pointer last color value returned");
  HQASSERT(pci != NULL, "NULL pointer to returned colorant index");
  HQASSERT(pcv != NULL, "NULL pointer to returned colorant value");

  /* Pick up pm from where we left off and check if we are in a catch all*/
  ppm = pdci->ppmLast;

  if ( ppm[0] == PM_CMD ) {
    /* Got pm command code - decide how to handle */

    switch ( ppm[1] ) {
    case PM_CMD_ALLSEP:
      if ( pdci->ciLast == COLORANTINDEX_ALL )
        return DLC_ITER_NOMORE ;
      *pci = pdlc->ci = pdci->ciLast = COLORANTINDEX_ALL ;
      *pcv = pdlc->cv = *(pdci->pcvLast);
      return DLC_ITER_ALLSEP;
    case PM_CMD_NONE:
      return DLC_ITER_NONE;
    case PM_CMD_ALL0:
    case PM_CMD_ALL1:
      return DLC_ITER_ALL01;
    case PM_CMD_OPACITY:
    case PM_CMD_OPMAXBLT:
      return DLC_ITER_NOMORE;
    default:
      HQFAIL ("dlc_next_colorant: unexpected cmd");
      return DLC_ITER_NOMORE;
    }
  }

  pmCheck = (paint_mask_t)(pdci->pmLast >> 1);

  if ( (pmCheck == 0) && !PM_CHAIN_SET(*ppm) ) {
    /* Quick check for already reached end of paint masks */
    return DLC_ITER_NOMORE;
  }

  /* Pick up index to start counting from */
  ci = pdci->ciLast;

  do {
    if ( pmCheck == 0 ) {
      /* No colorant and reached end of paint mask - look for next mask with a bit set */

      while ( (*ppm != PM_CMD) && PM_CHAIN_SET(*ppm) ) {
        /* Walk over paint masks with no colorant bits and not special commands */
        ppm++;

        if ( PM_GET_MASK(*ppm) != 0 ) {
          /* Got a paint mask with a colorant bit set */
          break;
        }

        /* Skipping this paint mask - update colorant index count */
        ci += PM_COLORANT_BITS;
      }

      if ( *ppm != PM_CMD ) {
        /* Got plain ol paint mask */

        if ( PM_GET_MASK(*ppm) == 0 ) {
          /* No colorant found and reached end of paint masks -  quit searching */
          return DLC_ITER_NOMORE;
        }

      } else { /* Got pm command */

        if (! PM_IS_CMD_ALLSEP(ppm[1]))
          return DLC_ITER_NOMORE;

        /* Remember pm looked at and colorant index */
        pdci->ppmLast = ppm;

        /* Return next colorant value and advance to next possible */
        *pci = pdlc->ci = pdci->ciLast = COLORANTINDEX_ALL;
        *pcv = pdlc->cv = *(++pdci->pcvLast);

        return DLC_ITER_ALLSEP;
      }

      /* Start checking paint mask from first index bit - MSB */
      pmCheck = PM_FIRST_COLORANT;
    }

    HQASSERT(*ppm != PM_CMD,
             "dlc_next_colorant: encountered unexpected paint mask command code");

    /* Got normal pm - pick up the paint mask */
    pm = PM_GET_MASK(*ppm);

    /* Find first set bit in paint mask counting the index */
    do {
      ci++;
      fGotColorant = (pmCheck & pm) != 0;
    } while ( !fGotColorant && ((pmCheck >>= 1) != 0) );

  } while ( !fGotColorant && PM_CHAIN_SET(*ppm) );

  /* Remember last paint mask looked at */
  pdci->ppmLast = ppm;

  if ( fGotColorant ) {
    /* We found another colorant to return - remember last check mask */
    pdci->pmLast = pmCheck;
    pdci->ciLast = ci;

    /* Return next colorant value and advance to next possible */
    *pci = pdlc->ci = pdci->ciLast;
    *pcv = pdlc->cv = *(++pdci->pcvLast);
  }

  return (fGotColorant ? DLC_ITER_COLORANT : DLC_ITER_NOMORE);

} /* Function dlc_next_colorant */


/** \brief Get reference to constant black dl color. */
void dlc_get_black(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdlc->ce.prc == NULL, "dl color entry still refers to another");

  *pdlc = context->dlc_black;

} /* Function dlc_get_black */


/** \brief Get reference to constant white dl color. */
void dlc_get_white(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdlc->ce.prc == NULL, "dl color entry still refers to another");

  *pdlc = context->dlc_white;

} /* Function dlc_get_white */


/** \brief Get reference to constant none dl color. */
void dlc_get_none(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  /*@notnull@*/ /*@out@*/ dl_color_t *pdlc)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");
  HQASSERT(pdlc->ce.prc == NULL, "dl color entry still refers to another");

  *pdlc = context->dlc_none;

} /* Function dlc_get_none */


/** \brief Change an ncolor to black.
 *
 * \param[in] context        DL color context.
 * \param[in,out] pp_ncolor  Pointer to ncolor to test
 */
void dl_to_black(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*  pp_ncolor    /* I/O */)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor != NULL, "dl_to_black: pp_ncolor NULL");

  if ( &(*pp_ncolor)->refs != context->dlc_black.ce.prc ) {
    dl_release(context, pp_ncolor);

    *pp_ncolor = (p_ncolor_t)context->dlc_black.ce.prc;
  }

} /* Function dl_to_black */


/** \brief Change an ncolor to white.
 *
 * \param[in] context        DL color context.
 * \param[in,out] pp_ncolor  Pointer to ncolor to test
 */
void dl_to_white(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*  pp_ncolor    /* I/O */)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor != NULL, "dl_to_white: pp_ncolor NULL");

  if ( &(*pp_ncolor)->refs != context->dlc_white.ce.prc ) {
    dl_release(context, pp_ncolor);

    *pp_ncolor = (p_ncolor_t)context->dlc_white.ce.prc;
  }

} /* Function dl_to_white */


/** \brief Change an ncolor to none.
 *
 * \param[in] context        DL color context.
 * \param[in,out] pp_ncolor  Pointer to ncolor to test
 */
void dl_to_none(
  /*@notnull@*/ /*@in@*/ dlc_context_t *context,
  p_ncolor_t*  pp_ncolor    /* I/O */)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  HQASSERT(pp_ncolor != NULL, "dl_to_none: pp_ncolor NULL");

  if ( &(*pp_ncolor)->refs != context->dlc_none.ce.prc ) {
    dl_release(context, pp_ncolor);

    *pp_ncolor = (p_ncolor_t)context->dlc_none.ce.prc;
  }

} /* Function dl_to_none */


/** \brief Test for ncolor being for NONE separation.
 *
 * \param p_ncolor    Pointer to ncolor to test
 *
 * \retval TRUE if \a p_ncolor points to constant NONE separation color
 * \retval FALSE if \a p_ncolor does not point to constant NONE separation color
 *
 * \note Assumes no clever ass creates copy of constant NONE paint mask, etc.
 */
Bool dl_is_none(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t  p_ncolor)
{
  HQASSERT(p_ncolor != NULL, "dl_is_none: NULL pointer to n color");

  /* If we VMError in the middle of rendering, it is possible to get a DL
   * where the above assert is not always true, and we crash. Rather than
   * trying to fix all the intricate logic for this case, just stop the
   * code from crashing at least.
   * \todo BMJ 24-Apr-14 : Remove assert or fix VMError logic for this case.
   */
  if ( p_ncolor == NULL )
    return FALSE;

  return (pm_special_type(pm_from_ncolor(p_ncolor)) == PM_NONE) ;
} /* Function dl_is_none */


/** \brief Test for dl color being for NONE separation.
 *
 * \param pdlc        Pointer to dl color to test
 *
 * \retval TRUE if \a pdlc points to constant NONE separation color
 * \retval FALSE if \a pdlc does not point to constant NONE separation color
 *
 * \note Assumes no clever ass creates copy of constant NONE paint mask, etc.
 */
Bool dlc_is_none(
  const dl_color_t* pdlc)     /* I */
{
  HQASSERT(pdlc != NULL, "dlc_is_none: NULL pointer to dl color");

  return (pm_special_type(pm_from_dlc(pdlc)) == PM_NONE) ;
} /* Function dlc_color_is_none */


/* See header for doc. */
Bool dlc_is_black(const dl_color_t *dlc, COLORANTINDEX blackIndex)
{
  COLORVALUE cv ;

  HQASSERT(dlc != NULL, "dlc_is_black: NULL pointer to dl color");

  if (pm_special_type(pm_from_dlc(dlc)) == PM_BLACK)
    return TRUE;

  if ( blackIndex == COLORANTINDEX_ALL ) {
    dl_color_iter_t iterator ;

    switch ( dlc_first_colorant(dlc, &iterator, &blackIndex, &cv) ) {
    default:
      HQFAIL("Unhandled iterator constant.");
      return FALSE;

    case DLC_ITER_ALL01: /* Special white */
    case DLC_ITER_NONE:
      return FALSE ;

    case DLC_ITER_ALLSEP: /* Additive All */
      return cv == COLORVALUE_ZERO;

    case DLC_ITER_COLORANT:
      do {
        if (cv != COLORVALUE_ZERO)
          return FALSE;
      } while (dlc_next_colorant(dlc, &iterator, &blackIndex, &cv) == DLC_ITER_COLORANT) ;
      return TRUE ;
    }
  } else if ( blackIndex != COLORANTINDEX_NONE ) {
    HQASSERT(blackIndex >= 0, "Invalid colorantindex") ;
    if ( dlc_get_indexed_colorant(dlc, blackIndex, &cv) &&
         cv == COLORVALUE_ZERO )
      return TRUE ;
  }

  return FALSE ;
}

/* See header for doc. */
Bool dlc_is_white(const dl_color_t* dlc)
{
  dl_color_iter_t iterator;
  COLORANTINDEX index;
  COLORVALUE c;

  /* Note that color values are held in additive form, so all 1's is white for
   * both RGB and CMYK. */
  switch (dlc_first_colorant(dlc, &iterator, &index, &c)) {
  default:
    HQFAIL("Unhandled iterator constant.");
    return FALSE;

  case DLC_ITER_NONE:
    return FALSE ;

  case DLC_ITER_ALL01:
    return pm_special_type(pm_from_dlc(dlc)) != PM_BLACK ;

  case DLC_ITER_ALLSEP:
    return c == COLORVALUE_ONE;

  case DLC_ITER_COLORANT:
    do {
      if (c != COLORVALUE_ONE)
        return FALSE;
    } while (dlc_next_colorant(dlc, &iterator, &index, &c) == DLC_ITER_COLORANT) ;
    return TRUE;
  }
}

/* ---------------------------------------------------------------------- */
/**
 * The handling of dlc_currentcolor, currentopacity and currentspflags is
 * entirely up to the calling code.  dlc_currentcolor is usually set in
 * gsc_invokeChainSingle and then copied into the p_ncolor reference of
 * LISTOBJECT currently being made.  currentopacity is for shfills with an alpha
 * channel.
 */

dl_color_t *dlc_currentcolor(dlc_context_t *context)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  return &context->dlc_currentcolor;
}

COLORVALUE dl_currentopacity(dlc_context_t *context)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  return context->currentopacity;
}

void dl_set_currentopacity(dlc_context_t *context, COLORVALUE opacity)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  context->currentopacity = opacity;
}

uint8 dl_currentspflags(dlc_context_t *context)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  return context->currentspflags;
}

void dl_set_currentspflags(dlc_context_t *context, uint8 spflags)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  context->currentspflags = spflags;
}

GSC_BLACK_TYPE dl_currentblacktype(dlc_context_t *context)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  return context->currentblacktype;
}

void dl_set_currentblacktype(dlc_context_t *context, GSC_BLACK_TYPE blacktype)
{
  VERIFY_OBJECT(context, DLC_CONTEXT_NAME);
  context->currentblacktype = blacktype;
}

/* ---------------------------------------------------------------------- */

/** \brief dl_num_channels returns the number of channels in the dl color.
 *
 * \param[in] p_ncolor   pointer to ncolor to test
 *
 * \returns number of channels.
 */
int32 dl_num_channels(
 /*@notnull@*/ /*@in@*/ const p_ncolor_t p_ncolor)
{
  color_entry_t ce;

  ce_from_ncolor(&ce, p_ncolor);

  return ce.ccv;
}

/* ---------------------------------------------------------------------- */

/** dl_color_opacity returns the opacity (aka alpha) associated with a dl color.
    Normally the value returned will be COLORVALUE_ONE, meaning opaque.  (Used
    for gradients with an opacity channel.) */
COLORVALUE dl_color_opacity(const p_ncolor_t ncolor)
{
  color_entry_t ce;
  HQASSERT(ncolor, "ncolor is null");
  ce_from_ncolor(&ce, ncolor);
  return ce.opacity;
}

COLORVALUE dlc_color_opacity(const dl_color_t* dlcolor)
{
  HQASSERT(dlcolor, "dlcolor is null");
  return dlcolor->ce.opacity;
}

/* ---------------------------------------------------------------------- */

#if defined( ASSERT_BUILD )

/** \brief Return current colorant index.
 *
 * \param[in] pdlc        Pointer to dl color entry.
 *
 * \returns     Current colorant index
 */
COLORANTINDEX dlc_colorant_index_(
           /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc)
{
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  return DLC_GET_INDEX(pdlc);

} /* Function dlc_colorant_index_ */


/** \brief Return current indexed colorant.
 *
 * \param[in] pdlc        Pointer to dl color entry.
 *
 * \returns     Current colorant value
 */
COLORVALUE dlc_get_colorant_(
      /*@notnull@*/ /*@in@*/ const dl_color_t *pdlc)
{
  HQASSERT(pdlc != NULL, "NULL pointer to dl color entry");

  return DLC_GET_COLORANT(pdlc);

} /* Function dlc_get_colorant_ */

#endif /* ASSERT_BUILD */


#if defined(VALGRIND_BUILD)

/** Replace the colorant buffer before each use to catch uninitalised data */
static void debug_scribble_buffer(dlc_context_t *context, int num_colorants)
{
  UNUSED_PARAM(int, num_colorants);
  if (context->grgbColorBuffer != NULL)
    dl_free(context->pools, context->grgbColorBuffer,
            DLC_BUFFER_SIZE * sizeof(uint8), MM_ALLOC_CLASS_NCOLOR);
  context->grgbColorBuffer =
              dl_alloc(context->pools, DLC_BUFFER_SIZE * sizeof(uint8),
                       MM_ALLOC_CLASS_NCOLOR);
}

#elif defined(DEBUG_BUILD)

/** Scribble over the colorant buffer prior to creating a new dl color */
static void debug_scribble_buffer(dlc_context_t *context, int num_colorants)
{
  int i;
  int n_slots_to_scribble;

  HQASSERT(num_colorants >= 0 && num_colorants <= BLIT_MAX_COLOR_CHANNELS,
           "Unexpectedly too many colorants");

  /* The number of slots that will be used can't be determined exactly a priori.
   * So take a guess at an overestimate without letting performance suffer.
   */
  n_slots_to_scribble = 3 * num_colorants + 10;
  if (n_slots_to_scribble >= DLC_BUFFER_SIZE)
    n_slots_to_scribble = DLC_BUFFER_SIZE;

  /* This scribble value should catch unexpected uses of it because in the paint
   * mask it will force a chain onto the next value, while a 16 bit color value
   * of 0xFFFF isn't valid.
   */
  for (i = 0; i <  n_slots_to_scribble; i++)
    context->grgbColorBuffer[i] = 0xFF;
}
#endif  /* DEBUG_BUILD || VALGRIND_BUILD */


#if defined(DEBUG_BUILD)

/** Dump full information on the DL colors, to make debugging easier. */
void debug_print_ce(color_entry_t *pce, Bool verbose)
{
  paint_mask_t *ppm = pce->ppm ;
  int32 pi ;
  Bool cmdok = TRUE ;
  int32 ci = 0 ;
  uint8 *format = (uint8 *)"%d" ;

  if ( verbose )
    monitorf((uint8 *)"ce 0x%08x refcount=%d ", pce, *(pce->prc)) ;
  monitorf((uint8 *)"colorants[");

  for ( pi = 0 ; pi < pm_findsize(ppm) ; ++pi ) {
    if ( cmdok && PM_CMD == ppm[pi] ) {
      switch ( ppm[pi + 1] ) {
      case PM_CMD_ALL0:
        monitorf((uint8 *)" /All0") ;
        break ;
      case PM_CMD_ALL1:
        monitorf((uint8 *)" /All1") ;
        break ;
      case PM_CMD_ALLSEP:
      case PM_CMD_ALLSEP|PM_CHAIN:
        monitorf((uint8 *)" /All") ;
        break ;
      case PM_CMD_NONE:
        monitorf((uint8 *)" /None") ;
        break ;
      case PM_CMD_OPACITY:
      case PM_CMD_OPACITY|PM_CHAIN:
        monitorf((uint8 *)" /Opacity") ;
        break ;
      case PM_CMD_OPMAXBLT:
        monitorf((uint8 *)"] maxblt indices [") ;
        ci = 0 ;
        format = (uint8 *)"%d" ;
        cmdok = FALSE ;
        break ;
      default:
        monitorf((uint8 *)"CMD(%x) ", ppm[pi + 1]) ;
        break ;
      }
      ++pi ;
    } else { /* Chain set */
      int32 i ;

      for ( i = 0 ; i < PM_COLORANT_BITS ; ++i, ++ci ) {
        if ( PM_COLORANT_SET(ppm[pi], PM_BIT_INDEX(i)) ) {
          monitorf(format, ci) ;
          format = (uint8 *)" %d" ;
        }
      }
    }
  }
  monitorf((uint8 *)"]") ;

  if ( pce->ccv > 0 ) {
    COLORVALUE *pcv = pce->pcv ;
    int32 ccv = pce->ccv ;

    monitorf((uint8 *)" colors [") ;
    format = (uint8 *)"%d" ;
    while ( ccv-- ) {
      monitorf(format, *pcv++) ;
      format = (uint8 *)" %d" ;
    }
    monitorf((uint8 *)"]") ;
  } else
    monitorf((uint8 *)" no colors") ;

  monitorf((uint8 *)"\n") ;
}

void debug_print_ncolor(p_ncolor_t p_ncolor)
{
  color_entry_t ce;

  ce_from_ncolor(&ce, p_ncolor) ;

  monitorf((uint8 *)"p_ncolor_t 0x%08x:\n ", p_ncolor) ;
  debug_print_ce(&ce, TRUE) ;
  monitorf((uint8 *)"\n") ;
}

void debug_print_dlc(dl_color_t *pdlc)
{
  HQASSERT(pdlc != NULL,
           "dlc_debug_print: NULL pointer to dl color entry");
  if ( pdlc->ce.prc ) {
    monitorf((uint8 *)"dl_color_t 0x%08x:\n ", pdlc) ;
    debug_print_ce(&pdlc->ce, TRUE) ;
    monitorf((uint8 *)"\n cached index=%d cached color=%d\n",
             pdlc->ci, pdlc->cv) ;
  } else
    monitorf((uint8 *)"dl_color_t 0x%08x is NULL\n", pdlc) ;
}

#endif /* DEBUG_BUILD */

/* Log stripped */
