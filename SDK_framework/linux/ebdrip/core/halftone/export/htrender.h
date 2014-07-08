/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!export:htrender.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Halftone operations for rendering.
 */

#ifndef __HTRENDER_H__
#define __HTRENDER_H__ 1

#include "bitbltt.h" /* FORM */
#include "mlock.h" /* multi_rwlock_t */
#include "graphict.h" /* GUCR_CHANNEL */
#include "displayt.h" /* dl_erase_nr */

struct LISTCHALFTONE; /* from htcache.h */


typedef int ht_render_type;
/* Be careful if you change the order or number of these; they are used in
   render.c to initialise arrays of blit functions, which will also need
   changing. */
enum ht_render_type {
  SPECIAL,
  ONELESSWORD,
  ORTHOGONAL,
  GENERAL,
  SLOWGENERAL,
  NHALFTONETYPES
} ;


/* ht_params_t - parameters used by halftone rendering and generation */
typedef struct ht_params_t {
  SPOTNO spotno;
  HTTYPE objtype;
  COLORANTINDEX ci;
  FORM *form;     /**< Selected halftone cell form. */
  struct LISTCHALFTONE *listchptr;
  FORM **cachedforms;
  void *lockid;
  int32 cx, cy;   /**< Current converged X, Y. */
  int32 px, py;   /**< Phase X (bits), Y (pixels). */
  int32 r1, r4;   /**< X components of cell (r1 + r4) = width. */
  int32 r2, r3;   /**< Y components of cell (r2 + r3) = height. */
  int32 *ys;      /**< Offsets to start of cell data in each line. */
  ht_render_type type; /**< Halftone type. */
  int32 rotate;   /**< Blit width modulo cell width in bits for ONELESSWORD. */
  int32 repeatb;  /**< Byte repeat to replicate cells (LCM of width & 8). */
  int32 repeatbw; /**< Word repeat to replicate cells (repeatb rounded down). */
  int32 xdims;    /**< Actual cell width in bits. */
  int32 ydims;    /**< Actual cell height in pixels. */
  int32 exdims;   /**< Cell width including orthogonal replications. */
  int32 eydims;   /**< Cell height including orthogonal replications. */
} ht_params_t;


extern multi_rwlock_t ht_lock;

#define HT_LOCK_WR_CLAIM(id) \
  multi_rwlock_lock(&ht_lock, (void *)(id), MULTI_RWLOCK_WRITE)
#define HT_LOCK_RD_CLAIM(id) \
  multi_rwlock_lock(&ht_lock, (void *)(id), MULTI_RWLOCK_READ)
#define HT_LOCK_RELEASE() multi_rwlock_unlock(&ht_lock)
#define HT_LOCK_WR_TO_RD(id) multi_rwlock_wr_to_rd(&ht_lock, (void *)(id))
#define HT_LOCK_CHECK(id) \
  multi_rwlock_check(&ht_lock, (void *)(id), MULTI_RWLOCK_READ)


/** LOCK_HALFTONE should be called to protect read access to the halftone
 * forms during rendering before calling GET_FORM().
 *
 * UNLOCK_HALFTONE is called to release the lock.
 *
 * Note: It is only necessary to actually claim a lock if lockid has
 * been set up by render_gethalftone().
 */

#define LOCK_HALFTONE(ht_params) MACRO_START                            \
  if ((ht_params)->lockid != NULL)                                      \
    HT_LOCK_RD_CLAIM((ht_params)->lockid);                              \
MACRO_END

#define UNLOCK_HALFTONE(ht_params) MACRO_START                          \
  if ((ht_params)->lockid != NULL) {                                    \
    HQASSERT(HT_LOCK_CHECK((ht_params)->lockid),                        \
             "UNLOCK_HALFTONE: the expected halftone is not locked");   \
    HT_LOCK_RELEASE();                                                  \
  }                                                                     \
MACRO_END

#define UNLOCK_HALFTONE_IF_WANTED(ht_params, released) MACRO_START      \
  if (ht_params != NULL && (ht_params)->lockid != NULL) {               \
    HQASSERT(HT_LOCK_CHECK((ht_params)->lockid),                        \
             "UNLOCK_HALFTONE_IF_WANTED: the expected halftone is not locked"); \
    released = multi_rwlock_unlock_if_wanted(&ht_lock);                 \
  }                                                                     \
MACRO_END

#define CHECK_HALFTONE_LOCK(ht_params)                                  \
  HQASSERT((ht_params)->lockid == NULL || HT_LOCK_CHECK((ht_params)->lockid), \
    "CHECK_HALFTONE_LOCK: halftone needs to be locked but is not")


/* The cachedforms field points to the form array for the current screen.
 * If the form is not cached for the current screen, a call to
 * getnearest generates the form from the closest form.
 */

/** Make sure the halftone form slot is populated. */
#define GET_FORM( index, htp ) MACRO_START                              \
  ht_params_t *_htp_ = (htp) ;                                          \
  CHECK_HALFTONE_LOCK( _htp_ ) ;                                        \
  HQASSERT(_htp_->cachedforms != NULL, "GET_FORM: cachedforms NULL");   \
  if ( (_htp_->form = _htp_->cachedforms[ index ]) == NULL )            \
    getnearest((index), _htp_);                                         \
MACRO_END


extern void getnearest( int32 index, const ht_params_t *ht_params );


#define HT_PARAMS_DEGENERATE(ht_params) ((ht_params)->listchptr == NULL)


/** Returns the screen index for RLE. */
int32 ht_screen_index(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci);


/** Prepares a screen for rendering.

  \param[out] ht_params  Pointer to a \c ht_params_t structure to fill in.
  \param[in] spotno  The spotno of the screen to render.
  \param[in] type  The object type of the screen to render.
  \param[in] ci  The colorant index of the screen to render.

  All the details necessary for rendering are loaded into \a ht_params.
 */
void render_gethalftone(ht_params_t* ht_params,
                        SPOTNO spotno, HTTYPE type, COLORANTINDEX ci,
                        corecontext_t *context);


/** Prepare for rendering screens on the indicated sheet.

  \param[in] erasenr  The erase number of the DL being rendered.
  \param[in] hf  The first channel on the sheet to be rendered.

  This will preload screen forms for the sheet. */
Bool ht_start_sheet(corecontext_t *context,
                    dl_erase_nr erasenr, GUCR_CHANNEL* hf);


/** Finish rendering screens on the indicated sheet.

  \param[in] erasenr  The erase number of the DL being rendered.
  \param[in] hf  The first channel on the sheet rendered.
  \param[in] report  Whether to report the screens used.

  This will discard screen forms for the sheet. */
void ht_end_sheet(dl_erase_nr erasenr, GUCR_CHANNEL* hf, Bool report);


struct MODHTONE_REF *ht_getModularHalftoneRef (
  SPOTNO spotno,
  HTTYPE type,
  COLORANTINDEX ci);


/** Tests if the given ht instance is set up for the colorant given. */
Bool ht_modularHalftoneUsedForColorant(const struct MODHTONE_REF *mht,
                                       dl_erase_nr erasenr,
                                       COLORANTINDEX ci);

/** Indicates if any non-modular halftones have been used in the DL. */
Bool ht_anyInRIPHalftonesUsed(dl_erase_nr erasenr);

Bool ht_is_object_based_screen(SPOTNO spotno);


#endif

/* Log stripped */
