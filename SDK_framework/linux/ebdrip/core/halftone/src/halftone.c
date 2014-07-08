/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:halftone.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Performs all the house-keeping for the halftones in the display list.
 */

#include "core.h"

#include "halftone.h"
#include "chalftone.h"
#include "htpriv.h"

#include "swcopyf.h"
#include "swerrors.h"
#include "swoften.h"
#include "swdevice.h"
#include "swstart.h"
#include "swtrace.h" /* SW_TRACE_INVALID */
#include "coreinit.h"
#include "eventapi.h"
#include "corejob.h"
#include "objects.h"
#include "dictscan.h"
#include "monitor.h"
#include "monitori.h"
#include "mm.h"
#include "mps.h"
#include "mmcompat.h"
#include "deferred.h"
#include "gcscan.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "devices.h" /* progressdev */
#include "hqbitops.h"
#include "namedef_.h"

#include "often.h"
#include "dl_color.h" /* COLORANTINDEX */
#include "bitbltt.h"  /* blit_t */
#include "bitblth.h" /* area0fill */
#include "htblits.h" /* bitexpandform */
#include "matrix.h"
#include "constant.h"       /* EPSILON */
#include "params.h"
#include "stacks.h"
#include "miscops.h"
#include "display.h"
#include "ndisplay.h"
#include "dicthash.h"       /* internaldict */
#include "graphics.h"
#include "progress.h"
#include "dlstate.h"
#include "render.h"
#include "plotops.h"
#include "gstate.h"
#include "control.h" /* low_mem_handle_between_ops */
#include "stackops.h"
#include "formOps.h"

#include "interrupts.h"
#include "swevents.h"
#include "mlock.h"
#include "gs_spotfn.h"
#include "gu_hsl.h" /* SPOT_FUN_RAND */
#include "gstack.h"
#include "gu_chan.h"
#include "gschtone.h" /* ht_extract_spotfunction */

#include "security.h"       /* fSECAllowLowResHDS */
/* Exporting Screens to the %screen% device OR %screeningaccleerator% device */
#include "exphps.h"

#include "ripdebug.h" /* for register_ripvar */

#include "gu_htm.h"         /* MODHTONE_REF */
#include "basemap.h"
#include "rleColorantMapping.h" /* RleColorantMap */
#include "metrics.h"

/** A record tracking which tint levels of a screen are used by a DL. */
typedef struct LEVELSRECORD {
  dl_erase_nr erasenr; /**< The erase number of the DL this is tracking for. */
  int32 number_cached; /**< Number of forms currently cached here. */
  int32 levels_used;   /**< Number of levels used for this screen. */
  FORM** levels; /**< Array of \c FORM*, (notones + 1) long. */
  Bool preloaded; /**< Indicates \a levels is used for rendering. */
  struct LEVELSRECORD *next; /**< A circular list of records for different DLs. */
#ifdef LEVEL_TRACKING
  Bool *level_used; /**< Track which levels are used, for debugging. */
#endif
} LEVELSRECORD ;

#define theILevelsUsed(val)    ((val)->levels_used)
#define theINumberCached(val)  ((val)->number_cached)
#define theICachedForms(val)   ((val)->levels)


/** Marks a modular screen used, and asserts that this is always done in the
    front end. */
#define htm_set_used_asserted(res, mhtref, dl) MACRO_START \
  if ( (dl) == input_dl ) \
    (res) = htm_set_used(mhtref, dl); \
  else { \
    HQASSERT(htm_is_used(mhtref, dl), "Unmarked screen in back end"); \
    (res) = TRUE; \
  } \
MACRO_END


/* This structure must be the same size as the FORM structure, and have
 * the fields in the same place. */
/** \todo PPP 2014-04-23 This needs to separated from FORM, because all fields
    could be in ht_params_t, and the free link overlaid on the raster buffer. */
typedef struct HTFORM {
  int32 type ;          /* MUST be first element - see CHARCACHE data type */
  blit_t *addr ;        /* Also addr must be in the same place as a FORM */
  int32 w , h , l , size ;
  struct HTFORM *nextf ;
} HTFORM ;

/* The htform_keep feature reuses form->hoff for a flag to indicate if the form
   contents have been inited: 0 no, HT_FORM_INITED yes.  */
#define HT_FORM_INITED 1


/** Class for halftone forms which have the same size (screen cell). */
typedef struct FORMCLASS {
  size_t formsize;
  FORM *form_chain; /**< List of forms cached here in the class. */
  size_t num_forms; /**< # forms cached on \c form_chain. */
  CHALFTONE *mru_chptr; /**< MRU end of MRU DLL of screens using this class. */
  CHALFTONE *lru_chptr; /**< LRU end of MRU DLL of screens using this class. */
  size_t num_screens; /**< # screens using this class overall. */
  size_t num_screens_sheet; /**< # screens using this class on the current sheet. */
  size_t levels_reqd; /**< # forms required by the current sheet. */
  size_t levels_cached; /**< # forms cached in the screens for the current sheet. */
  dl_erase_nr erasenr; /**< The DL that owns forms cached here. */
  struct FORMCLASS *next_formclass ;
} FORMCLASS ;


dl_erase_nr oldest_dl;

/** The erase number of the DL being rendered. */
static dl_erase_nr output_dl;

dl_erase_nr input_dl;


/** Flag for keeping forms instead of discarding them. */
static Bool ht_form_keep;


/* While the display list is being built up, the form array pointed to by
 * a halftone cache is used as a tag array to indicate whether that level
 * has been used, and whether a form has been allocated for it:
 *  NULL  -  the gray level has not been used, and no form is allocated
 *  INVALID_FORM - the gray level has been used, but no form allocated
 *  a pointer   -  the gray level has been used, and a form is allocated
 */
static FORM invalidform ;
static FORM deferredform ;

#define DEFERRED_FORM (&deferredform) /* marker for deferred allocations */
#define FORM_LEVEL_IS_USED  (&invalidform)
#define INVALID_FORM        (&invalidform)


/** Nesting counter for deferred ht allocation (applies to front end only). */
static int deferring_allocation = 0;

multi_rwlock_t ht_lock;


static FORMCLASS *formclasses = NULL ;

/** Mutex for access to \c formclasses and the \c num_screens field therein. */
static multi_mutex_t formclasses_mutex;


static int32 gNumDeferred = 0; /* global count of deferred allocations */

static Bool preload_failed; /* Did preload get all the forms? */

static DEVICELIST *screendevice = NULL ;


static void freechalftone( CHALFTONE *chptr );

static void ht_report_screen_usage(dl_erase_nr erasenr, GUCR_CHANNEL* hf);

#if defined( ASSERT_BUILD )
static void report_ch_caching(dl_erase_nr erasenr);
#endif

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
Bool installing_hexhds = FALSE;
#endif


static uint32 theseed = 0 ;

#ifdef METRICS_BUILD
typedef struct halftone_metrics_t {
  int32 generations, generation_levels, generation_tints ;
  int32 regenerations, regeneration_levels, regeneration_tints ;
  int32 form_copies, form_resets ;
  size_t form_copy_bytes, form_reset_bytes ;
  int32 poached ;
} halftone_metrics_t ;

static halftone_metrics_t halftone_metrics = { 0 } ;

static Bool halftone_metrics_update(sw_metrics_group *metrics)
{
  if ( !sw_metrics_open_group(&metrics, METRIC_NAME_AND_LENGTH("Halftone")) )
    return FALSE ;

  SW_METRIC_INTEGER("PoachedForms", halftone_metrics.poached) ;
  SW_METRIC_INTEGER("Generations", halftone_metrics.generations) ;
  SW_METRIC_INTEGER("GenerationTints", halftone_metrics.generation_tints) ;
  SW_METRIC_INTEGER("GenerationLevels", halftone_metrics.generation_levels) ;
  SW_METRIC_INTEGER("Regenerations", halftone_metrics.regenerations) ;
  SW_METRIC_INTEGER("RegenerationTints", halftone_metrics.regeneration_tints) ;
  SW_METRIC_INTEGER("RegenerationLevels", halftone_metrics.regeneration_levels) ;
  SW_METRIC_INTEGER("FormCopies", halftone_metrics.form_copies) ;
  SW_METRIC_INTEGER("FormCopyBytes", (int32)halftone_metrics.form_copy_bytes) ;
  SW_METRIC_INTEGER("FormResets", halftone_metrics.form_resets) ;
  SW_METRIC_INTEGER("FormResetBytes", (int32)halftone_metrics.form_reset_bytes) ;

  sw_metrics_close_group(&metrics) ;

  return TRUE ;
}

static void halftone_metrics_reset(int reason)
{
  halftone_metrics_t init = { 0 } ;

  UNUSED_PARAM(int, reason) ;

  halftone_metrics = init ;
}

static sw_metrics_callbacks halftone_metrics_hook = {
  halftone_metrics_update,
  halftone_metrics_reset,
  NULL
} ;
#endif

/* -------------------------------------------------------------------------- */
/* Return a random number between 0 and 1 */

static double hqnrand( void )
{
  theseed = 2147001325 * theseed + 715136305 ;
  return (double)( theseed >> 1 ) / (double)( 0x7fffffff ) ;
}


static mm_pool_t ht_form_pool;


void free_ht_form(FORM *form)
{
  mm_free(ht_form_pool, (mm_addr_t)form,
          (mm_size_t)(form->size + BLIT_ALIGN_SIZE(sizeof(FORM))));
}


/* --------------------------------------------------------------------- */
static FORM *make_ht_form( int32 w, int32 h, size_t formsize, mm_cost_t cost )
{
  /* allocate form and bitmap - no error handling if fails. */
  int32 lbytes ;
  FORM *this_form ;

  HQASSERT(formsize > 0, "formsize not > 0");

  /* Round up to nearest multiple of blit_t. */
  lbytes = FORM_LINE_BYTES(w) ;
  this_form = mm_alloc_cost(ht_form_pool,
                            formsize + BLIT_ALIGN_SIZE(sizeof(FORM)),
                            cost, MM_ALLOC_CLASS_HALFTONE_FORM);
  if (this_form == NULL)
    /* VMERROR is handled by clients */
    return NULL ;

  theFormA(*this_form) = BLIT_ALIGN_UP(this_form + 1) ;

  /* Set up FORM's values, */
  theFormT(*this_form) = FORMTYPE_HALFTONEBITMAP ;
  theFormW(*this_form) = w ;
  theFormH(*this_form) = h ;
  theFormL(*this_form) = lbytes ;
  theFormS(*this_form) = CAST_SIZET_TO_INT32(formsize);
  theFormRH(*this_form) = h ;
  theFormHOff(*this_form) = 0 ;

  return this_form ;
}

/* ---------------------------------------------------------------------- */
static void update_form( FORM *form , int32 w , int32 h )
{
  theFormT(*form) = FORMTYPE_HALFTONEBITMAP ;
  theFormW(*form) = w ;
  theFormH(*form) = h ;
  theFormL(*form) = FORM_LINE_BYTES(w);
  theFormRH(*form) = h ;
  theFormHOff(*form) = 0 ;
  /* leave formsize reflecting the storage allocation. */
}


/** Allocate a levels record for the given number of tones.

  \param[in] notones  Number of tones tracked by this record.
  \return The levels record.
 */
static LEVELSRECORD *levelsrecs_alloc(corecontext_t *context, uint16 notones)
{
  LEVELSRECORD *rec = NULL;
  int i;
  deferred_alloc_t *da = deferred_alloc_init(1);
  MEMORY_REQUEST_VARS(record, HALFTONE_LEVELS,
                      NUM_DISPLAY_LISTS * sizeof(LEVELSRECORD), 1);
  MEMORY_REQUEST_VARS(levels, HALFTONE_LEVELS, 0, NUM_DISPLAY_LISTS);
#ifdef LEVEL_TRACKING
  MEMORY_REQUEST_VARS(used, HALFTONE_LEVELS, 0, NUM_DISPLAY_LISTS);
#endif

  if ( da == NULL )
    return FAILURE(NULL);
  levels_request.size = (notones+1) * sizeof(FORM *);
#ifdef LEVEL_TRACKING
  used_request.size = (notones+1) * sizeof(Bool);
#endif
  if ( deferred_alloc_add_simple(da, &record_request,
                                 mm_pool_temp, record_blocks)
       && deferred_alloc_add_simple(da, &levels_request,
                                    mm_pool_temp, levels_blocks)
#ifdef LEVEL_TRACKING
       && deferred_alloc_add_simple(da, &used_request,
                                    mm_pool_temp, used_blocks)
#endif
       && deferred_alloc_realize(da, mm_cost_normal, context) ) {
    rec = record_blocks[0];
    for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) {
      rec[i].levels = levels_blocks[i];
#ifdef LEVEL_TRACKING
      rec[i].level_used = used_blocks[i];
#endif
    }
  }
  deferred_alloc_finish(da);
  return rec;
}


/** Initialize a levels record.

  \param[in] rec  The levels record to init.
  \param[in] notones  Number of tones tracked by this record.
  \param[in] erasenr  The erase number of the first page this is used on.
 */
static void levelsrecs_init(LEVELSRECORD *rec,
                            uint16 notones, dl_erase_nr erasenr)
{
  size_t i;

  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) {
    rec[i].erasenr = INVALID_DL;
    rec[i].number_cached = 0;
    rec[i].levels_used = 0;
    HqMemZero(rec[i].levels, (notones + 1) * sizeof(FORM*));
    rec[i].preloaded = FALSE;
#ifdef LEVEL_TRACKING
    HqMemZero(rec[i].level_used, (notones + 1) * sizeof(Bool));
#endif
    rec[i].next = i == NUM_DISPLAY_LISTS-1 ? &rec[0] : &rec[i+1];
  }
  rec[0].erasenr = erasenr;
}


/** Free a levels record.

  \param[in] rec  The levels record to free.
  \param[in] notones  Number of tones tracked by this record.
 */
static void levelsrecs_free(LEVELSRECORD *rec, uint16 notones)
{
  size_t i;

  UNUSED_PARAM(uint16, notones);
  for ( i = 0 ; i < NUM_DISPLAY_LISTS ; ++i ) {
#ifdef LEVEL_TRACKING
    mm_free(mm_pool_temp, rec[i].level_used, (notones+1) * sizeof(Bool));
#endif
    mm_free(mm_pool_temp, rec[i].levels, (notones+1) * sizeof(FORM *));
  }
  mm_free(mm_pool_temp, rec, NUM_DISPLAY_LISTS * sizeof(LEVELSRECORD));
}


#ifdef ASSERT_BUILD
static void levelsrecord_check(LEVELSRECORD *rec, uint16 notones)
{
  size_t nocached = 0, i;
  FORM **forms = rec->levels;

  for ( i = 0 ; i <= notones ; ++ i )
    if ( forms[i] != NULL
         && forms[i] != INVALID_FORM && forms[i] != DEFERRED_FORM )
      ++nocached;
  HQASSERT(nocached == (size_t)rec->number_cached, "Cached count wrong");
}
#else
#define levelsrecord_check(rec, notones)
#endif


/** Locate a levels record for the input DL in a CHALFTONE.

  \param[in] pch  The CHALFTONE structure whose input record is sought.
  \return The input levels record.

  If there isn't one for this DL yet, one is reserved for it. \c
  pch->usage is set to point to the record found. */
static LEVELSRECORD *chalftone_input_levels(CHALFTONE *pch)
{
  LEVELSRECORD *rec = pch->usage;

  HQASSERT(input_dl != INVALID_DL, "No input DL");
  if ( rec->erasenr == input_dl )
    return rec;
  while ( rec->erasenr != INVALID_DL ) { /* Find an unused one. */
    rec = rec->next;
    HQASSERT(rec != pch->usage, "Ran out of levels records");
  }
  HQASSERT(IS_INTERPRETER(), "Updating usage outside interpreter");
  pch->usage = rec;
  rec->erasenr = input_dl;
  return rec;
}


/** Locate a levels record for the given DL in a CHALFTONE.

  \param[in] pch  The CHALFTONE structure whose records are sought.
  \param[in] erasenr  The erase number for which the record is sought.
  \return The levels record or \c NULL.
*/
static LEVELSRECORD *chalftone_levels(CHALFTONE *pch, dl_erase_nr erasenr)
{
  LEVELSRECORD *rec, *start;

  if ( erasenr == output_dl ) { /* It's usually output. */
    rec = pch->forms;
    if ( rec != NULL && rec->erasenr == erasenr )
      return rec;
  }
  /* Guess didn't work out, scan the whole list. */
  start = rec = pch->usage; /* start here, it's often the one */
  /* We need the start var, as pch->usage may change during this loop. */
  do {
    if ( rec->erasenr == erasenr )
      return rec;
    rec = rec->next;
  } while ( rec != start );
  return NULL;
}


/** Locate a levels record for the output DL in a CHALFTONE.

  \param[in] pch  The CHALFTONE structure whose output record is sought.
  \return The output levels record or \c NULL.

  If the screen is used, a record must have been reserved at the front
  end (by \c chalftone_input_levels). \c pch->forms is set to point to
  the record found. */
static LEVELSRECORD *chalftone_output_levels(CHALFTONE *pch)
{
  LEVELSRECORD *rec = pch->forms, *start;

  HQASSERT(output_dl != INVALID_DL, "No output DL");
  if ( rec != NULL ) {
    if ( rec->erasenr == output_dl )
      return rec;
    else
      start = rec->next; /* Start from the next in queue then. */
  } else
    start = pch->usage; /* start here, it's the one when not pipelining */
  /* Scan the whole list. */
  rec = start;
  do {
    if ( rec->erasenr == output_dl ) {
      pch->forms = rec;
      return rec;
    }
    rec = rec->next;
  } while ( rec != start );
  return NULL;
}


/** Locate a levels record for the input or the output DL in a CHALFTONE.

  \param[in] pch  The CHALFTONE structure whose record is sought.
  \param[in] erasenr  The erase number for which the record is sought.
  \return The input or output levels record.

  This is called by functions that mark screen levels either in the
  front end or the back end. So \a erasenr must be either the input page
  or the output page. Because there is no explicit render-start call,
  this assumes that a new erase number means that page has just started
  rendering.

  \c pch->usage or \c pch->forms is set to point to the record found. */
static LEVELSRECORD *chalftone_io_levels(CHALFTONE *pch, dl_erase_nr erasenr)
{
  if ( erasenr == input_dl )
    return chalftone_input_levels(pch);
  else {
    LEVELSRECORD *rec;

    if ( erasenr != output_dl ) /* must be starting rendering, then  */
      output_dl = erasenr;
    rec = chalftone_output_levels(pch);
    HQASSERT(rec != NULL, "No levels record allocated");
    return rec;
  }
}


dl_erase_nr chalftone_last_used(CHALFTONE *pch)
{
  LEVELSRECORD *rec, *start;
  dl_erase_nr largest = pch->usage->erasenr;

  /* The last-used tracking relies on INVALID_DL < FIRST_DL. */
  /* pch->usage is the latest levels record, so if it's in use, it'll have the
     largest erasenr. pch->usage can't change during this function, because it's
     only changed by the interpreter, and this function is only called in the
     interpreter. */
  HQASSERT(IS_INTERPRETER(), "Trying to get last used DL outside interpreter");
  if ( largest != INVALID_DL )
    return largest;
  /* Scan all records to find largest erasenr. */
  for ( start = pch->usage, rec = start->next ; rec != start ; rec = rec->next ) {
    if ( rec->erasenr > largest )
      largest = rec->erasenr;
  }
  return largest;
}


/* ---------------------------------------------------------------------- */

/* Must be called holding formclasses_mutex. */
static void free_formclass( FORMCLASS *formclass )
{
  register FORM *t1 = formclass->form_chain ;
  register FORM *t2 ;

  while ( t1 ) {
    t2 = (FORM *)(((HTFORM *)t1)->nextf) ;
    free_ht_form( t1 );
    t1 = t2 ;
  }

  if ( formclasses == formclass ) {

    formclasses = formclass->next_formclass ;

  } else {
    FORMCLASS *pfclass = formclasses ;

    while ( pfclass->next_formclass != formclass )
      pfclass = pfclass->next_formclass ;

    pfclass->next_formclass = formclass->next_formclass ;
  }
  mm_free(mm_pool_temp, (mm_addr_t) formclass, sizeof(FORMCLASS) ) ;
}


/* Must be called holding formclasses_mutex. */
static FORMCLASS *alloc_formclass(size_t formsize)
{
  FORMCLASS *formclass = mm_alloc(mm_pool_temp, sizeof(FORMCLASS),
                                  MM_ALLOC_CLASS_FORMCLASS);

  if ( formclass == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  formclass->formsize = formsize;
  formclass->form_chain = NULL;
  formclass->num_forms = 0;
  formclass->lru_chptr = NULL; formclass->mru_chptr = NULL;
  formclass->num_screens = 0;
  formclass->num_screens_sheet = 0;
  formclass->levels_reqd = 0; formclass->levels_cached = 0;
  formclass->erasenr = INVALID_DL;

  /* insert it into the formclass list - largest class first */
  if ( (formclasses == NULL) || (formclasses->formsize < formsize) ) {
    formclass->next_formclass = formclasses;
    formclasses = formclass;
  } else {
    FORMCLASS *p = formclasses;

    while ( (p->next_formclass != NULL)
            && (p->next_formclass->formsize < formsize) )
      p = p->next_formclass;
    formclass->next_formclass = p->next_formclass;
    p->next_formclass = formclass;
  }
  return formclass;
}

/* ---------------------------------------------------------------------- */

/** Bump the reference count of a CHALFTONE */
void addRefChPtr( CHALFTONE *chptr )
{
  HQASSERT( chptr, "addRefChPtr: Null chptr" ) ;

  theIDuplicate(chptr)++ ;

  HQASSERT( theIDuplicate(chptr) > 0,
            "addRefChPtr: Unexpected negative or zero reference count in chptr" ) ;
}

/** Decrement the reference count of a CHALFTONE and free it if this was
 * the last reference to it.
 */
void releaseChPtr( CHALFTONE *chptr )
{
  HQASSERT( chptr, "releaseChPtr: Null chptr" ) ;

  if ( --theIDuplicate(chptr) > 0 )
    return;
  /* Last ref to screen details - free it off */
  freechalftone(chptr);
}


/* ---------------------------------------------------------------------- */
/** Cleans up allocations after a failed \c ht_insertchentry. */
static void freeqhalftone(CHALFTONE *newchalftone,
                          LISTCHALFTONE *newlistchalftone)
{
  if ( newchalftone->thxfer )
    mm_free_with_header(mm_pool_temp, newchalftone->thxfer);
  mm_free_with_header(mm_pool_temp,
                      /* rotated screen can have swapped x<->y */
                      newchalftone->xcoords < newchalftone->ycoords
                      ? newchalftone->xcoords : newchalftone->ycoords);
  if ( newchalftone->path )
    mm_free_with_header(mm_pool_temp, newchalftone->path);
  mm_free(mm_pool_temp, newchalftone, sizeof(CHALFTONE));
  if ( newlistchalftone != NULL )
    safe_free_listchalftone(newlistchalftone, FALSE);
}


/* ---------------------------------------------------------------------- */
static void freechalftone( CHALFTONE *chptr )
{
  FORM **forms;
  LEVELSRECORD *levelsrec;
  FORMCLASS *formclass ;

  formclass = chptr->formclass ;

  if ( ! formclass ) {
    /* a pattern screen */
    if ( chptr->pattern_form )
      free_ht_form( chptr->pattern_form );
    levelsrec = theILevels( chptr ) ;
    do {
      forms = theICachedForms( levelsrec ) ;
      if ( forms[ 1 ] && forms[ 1 ] != INVALID_FORM &&
           forms[ 1 ] != DEFERRED_FORM )
        free_ht_form( forms[1] );

      levelsrec = levelsrec->next ;
    } while ( levelsrec != theILevels( chptr )) ;
    levelsrecs_free(chptr->levels, chptr->notones);

  } else { /* a normal screen */

    if ( ht_form_keep ) {
      levelsrec = theILevels(chptr);
      do {
        if ( levelsrec->number_cached != 0 ) {
          FORM **forms = levelsrec->levels;
          size_t i;

          for ( i = 1 ; i < (size_t)chptr->notones-1 ; ++i )
            if ( forms[i] != NULL
                 && forms[i] != INVALID_FORM && forms[i] != DEFERRED_FORM )
              free_ht_form(forms[i]);
          /* This doesn't own the formclass chain, so can't put any there. */
        }
        levelsrec = levelsrec->next ;
      } while ( levelsrec != theILevels( chptr )) ;
    } else {
#if ASSERT_BUILD
      /* Walk over the levels array to verify there are no forms. This is called
         either when construction fails and the screen never had any forms, or
         after the pages it was used on have been retired, so the forms have
         been unloaded. */
      levelsrec = theILevels( chptr ) ;
      do {
        HQASSERT(levelsrec->number_cached == 0, "Shouldn't free screens in use.");
        levelsrec = levelsrec->next ;
      } while ( levelsrec != theILevels( chptr )) ;
#endif
    }
    levelsrecs_free(chptr->levels, chptr->notones);
    /* remove the CHALFTONE from the form class */
    multi_mutex_lock(&formclasses_mutex);
    if ( --formclass->num_screens == 0 )
      free_formclass( formclass ) ;
    multi_mutex_unlock(&formclasses_mutex);
  } /* if pattern/normal screen */

  mm_free_with_header(mm_pool_temp, (mm_addr_t) theIHalfYs( chptr )) ;

  if ( theITHXfer( chptr ))
    mm_free_with_header(mm_pool_temp, (mm_addr_t) theITHXfer( chptr )) ;
  if ( chptr->xcoords != NULL )
    mm_free_with_header(mm_pool_temp,
                        /* rotated screen can have swapped x<->y */
                        chptr->xcoords < chptr->ycoords
                        ? chptr->xcoords : chptr->ycoords);
  if ( chptr->path != NULL )
    mm_free_with_header(mm_pool_temp, (mm_addr_t) chptr->path );

  mm_free(mm_pool_temp, (mm_addr_t) chptr, sizeof(CHALFTONE) ) ;
}


void chalftone_scan(mps_ss_t ss, CHALFTONE *chptr)
{
  MPS_SCAN_BEGIN( ss )
    MPS_RETAIN( &chptr->sfname, TRUE );
  MPS_SCAN_END( ss );
}


void chalftone_restore_names(CHALFTONE *chptr, int32 slevel)
{
  if ( chptr->sfname != NULL && chptr->sfname->sid > slevel )
    chptr->sfname = NULL;
}


/* ---------------------------------------------------------------------- */
/* convert the internal name to an external name */
Bool ht_getExternalScreenName( uint8       *externalName,
                               NAMECACHE   *name,
                               int32       externalNameLength )
{
  OBJECT *spotnamesarray = NULL ;
  OBJECT *array = NULL ;
  OBJECT *namestring = NULL ;
  int32 i, arraylen ;

  if ( name == NULL)
    return FALSE;

  if ( NULL == (spotnamesarray  = fast_extract_hash_name(&internaldict,
                                                         NAME_SpotFunctionNames)))
    return FALSE ;
  if ( ( oType( *spotnamesarray ) != OARRAY ) &&
       ( oType( *spotnamesarray ) != OPACKEDARRAY ) )
    return FALSE ;
  arraylen = theLen(*spotnamesarray) ;
  /* points to the first array */
  if ( NULL == ( array = oArray(*spotnamesarray)))
      return FALSE ;
  for ( i = 0 ; i < arraylen ; i++ ) {
    if ( ( ( oType( *array ) != OARRAY ) &&
           ( oType( *array ) != OPACKEDARRAY )) ||
        ( theLen(*array) != 2 ) )
    return FALSE ;
    if ( NULL == ( namestring = oArray(*array)))
      return FALSE ;
    if ( oType( *namestring ) != OSTRING )
      return FALSE ;
    if ( HqMemCmp(oString(*namestring),
                  (int32)theLen(*namestring),
                  theICList(name), theINLen(name)) == 0 ) {
      int32   nameLength;
      /* Get the external name */
      namestring ++  ;
      if ( oType( *namestring ) != OSTRING )
        return FALSE ;
      if ( theLen(*namestring) >= LONGESTSCREENNAME )
        return FALSE ;
      nameLength = theLen(*namestring) < externalNameLength
                   ? theLen(*namestring) : externalNameLength - 1;
      HqMemCpy( externalName, (uint8 *)oString( *namestring ), nameLength ) ;
      externalName[nameLength] = '\0' ;
      return TRUE ;
    }
    array ++ ;
  }
  return FALSE ;
}

/* ---------------------------------------------------------------------- */


/* Returns the shift for raster depth. */
int32 ht_bit_depth_shift(GUCR_RASTERSTYLE *rs)
{
  if (rs == NULL)
    /* incredibly, first setscreen is before any raster style is set up */
    return 0;
  else
    return gucr_halftoning(rs) ? gucr_rasterDepthShift(rs) : 0;
}


#define CONVERGE_HALFTONEPHASE( _pxy , _exydims ) MACRO_START \
  int32 i ; \
  i = _pxy / _exydims ; \
  _pxy -= ( i * _exydims ) ; \
  if ( _pxy < 0 ) \
    _pxy += _exydims ; \
  HQASSERT( _pxy >= 0 && _pxy < _exydims , "phase xy not converged" ) ; \
MACRO_END

/** Converts user phase input (*cx_io, *cy_io) into rendering phase in
 * bits (*px_out, *py_out) and initial converged x & y (*cx_io, *cy_io). */
void ht_converge_phase(CHALFTONE *ch, int32 *cx_io, int32 *cy_io,
                       int32 *px_out, int32 *py_out)
{
  if ( ch != NULL ) { /* non-modular */
    int32 phasex = *cx_io << ch->depth_shift;
    int32 phasey = *cy_io;

    CONVERGE_HALFTONEPHASE( phasex, ch->halfexdims );
    CONVERGE_HALFTONEPHASE( phasey, ch->halfeydims );
    *cx_io = phasex; *cy_io = phasey;
    *px_out = ch->halfxdims - phasex; *py_out = ch->halfydims - phasey;
  } else { /* modular screens do their own convergence */
    *px_out = *cx_io; *py_out = *cy_io;
  }
}


int32 ht_screen_index(SPOTNO spotno, HTTYPE type, COLORANTINDEX ci)
{
  CHALFTONE *chptr;

  chptr = ht_getch(spotno, type, ci);
  /* Modular screens don't work with RLE, so no need to consider them. */
  if ( chptr == NULL )
    return -1;
  return theIScreenIndex(chptr);
}


FORM* ht_patternscreenform(
  SPOTNO        spotno,
  int32*        xx,
  int32*        xy,
  int32*        yx,
  int32*        yy,
  int32*        px,
  int32*        py)
{
  register LISTCHALFTONE* plistch;
  register CHALFTONE*     pch;

  HQASSERT(spotno > 0, "invalid spot number");

  plistch = ht_getlistch(spotno, HTTYPE_DEFAULT, COLORANTINDEX_NONE, NULL);
  if ( plistch != NULL ) {
    /* Found default screen for given spot number */
    pch = ht_listchChalftone(plistch);
    *xx = theIHalfR1(pch);
    *xy = theIHalfR2(pch);
    *yx = theIHalfR4(pch);
    *yy = -theIHalfR3(pch); /* convert vectors to device space */
    ht_listchCXY(plistch, px, py);
    if ( pch->pattern_form != NULL ) {
      return pch->pattern_form ;
    }
    return pch->usage->levels[1];
  }
  return NULL ;
}


void ht_defer_allocation( void )
{
  if ( get_core_context()->is_interpreter )
    /* Deferment is not thread-safe, so ignored outside interpreter. */
    ++deferring_allocation ;
}

/* When deferred forms are 'allocated' by ht_allocatechentry, a global count of
   deferred allocations, gNumDeferred is incremented, and a count local to the
   CHALFTONE structure, num_deferred is too.
   The global count gives us a quick way to detect whether there's still things
   to do, and the local one saves traversing all the levels in the innermost
   loop. */
void ht_resume_allocation(
  SPOTNO            spotno,
  Bool              success)
{
  LISTCHALFTONE*        plistch;
  CHALFTONE* pch;
  ht_cacheIterator iter;
  corecontext_t *context = get_core_context();
  FORM *state_for_failure = NULL;
  memory_requirement_t request;
  Bool suppress_pp = TRUE, no_error;

  HQASSERT(spotno > 0, "invalid spot number");

  if ( !context->is_interpreter )
    return; /* Deferment is not thread-safe, so ignored outside interpreter. */

  HQASSERT(deferring_allocation > 0,
           "ht_resume_allocation called when not deferred");
  HQASSERT(gNumDeferred >= 0, "ht_resume_alloc: gNumDeferred -ve");

  /* Early exit if we don't need it yet or there's nothing to do */
  if ( --deferring_allocation != 0 || gNumDeferred == 0 )
    return;

  request.pool = mm_pool_temp; request.cost = mm_cost_below_reserves;
  do { /* try the allocations and low-memory handling alternately */
    /* Use spotno as a clue for which chain to try first */
    ht_iterChentryBegin(&iter, input_dl, spotno, COLORANTINDEX_UNKNOWN);
    while ( ht_iterChentryNext(&iter, &plistch, &pch, NULL, NULL, NULL, NULL) ) {
      int32 nDeferred;
      uint16 maxLevels;
      LEVELSRECORD* plevelsrec;
      uint16 level;

      if ( pch == NULL )
        continue; /* No allocation for modular screens */

      nDeferred = pch->num_deferred;
      if ( nDeferred == 0 )
        continue; /* Nothing to do, particularly don't fetch levelsrec. */
      maxLevels = pch->notones;
      plevelsrec = chalftone_input_levels(pch);
      /* Go through and replace all deferred forms with real forms */
      for ( level = 1; level < maxLevels; level++ ) {
        FORM *form;

        if ( nDeferred == 0 )
          break;
        if ( theICachedForms(plevelsrec)[level] == DEFERRED_FORM ) {
          if ( ! success ) {    /* Don't create cache entry, we failed */
            theICachedForms(plevelsrec)[level] = state_for_failure;
            HQASSERT(state_for_failure == NULL || !plevelsrec->preloaded,
                     "Marking screen used after preload");
            --nDeferred;
            continue;
          }

          HQASSERT(pch->formclass,
                   "ht_resume_alloc: No formclass for deferred halftone cache entry\n");
          HQASSERT(theIHalfMXDims(pch) != 0, "ht_resume_alloc: got 0 width");
          HQASSERT(theIHalfMYDims(pch) != 0, "ht_resume_alloc: got 0 height");
          HQASSERT(pch->formclass->formsize != 0, "ht_resume_alloc: got 0 formsize");

          form = make_ht_form(theIHalfMXDims(pch), theIHalfMYDims(pch),
                              pch->formclass->formsize, mm_cost_below_reserves);
          /** \todo The actual cost of halftone forms should be determined. */
          if ( form == NULL )
            break;
          theICachedForms(plevelsrec)[level] = form;
          --nDeferred; ++theINumberCached(plevelsrec);
        }
      }
  #if defined( ASSERT_BUILD )
      /* Paranoid check */
      for ( ; nDeferred == 0 && level < maxLevels; level++ ) {
        HQASSERT(theICachedForms(plevelsrec)[level] != DEFERRED_FORM,
                 "ht_resume_alloc: more DEFERRED_FORMs than expected");
      }
  #endif
      if ( pch->num_deferred > 0 ) {
        gNumDeferred -= (pch->num_deferred - nDeferred);
        HQASSERT(gNumDeferred >= 0, "gNumDeferred -ve");
        pch->num_deferred = nDeferred;
        /* We can return as soon as we've seen everything */
        if ( gNumDeferred == 0 ) {
          ht_iterChentryEnd(&iter);
          return;
        }
        if ( nDeferred != 0 ) { /* ran out of memory */
          request.size = (size_t)nDeferred * pch->formclass->formsize;
          break;
        }
      }
    } /* cache entries */
    ht_iterChentryEnd(&iter);

    HQASSERT(success, "Should have finished if in failure mode");
    HQTRACE( debug_lowmemory,
             ( "CALL(low_mem_handle_between_ops): ht_resume_allocation" ));
    SwOftenSafe();
    no_error = low_mem_handle_between_ops(&success, &suppress_pp,
                                          context, 1, &request);
    HQASSERT(no_error, "Ignoring low-mem handler error");
    if ( !success )
      state_for_failure = INVALID_FORM;
  } while ( gNumDeferred != 0 );
} /* Function ht_resume_allocation */


/** Allocates a form in the levels array given. */
static void ht_allocatechentry(corecontext_t *context,
                               CHALFTONE* pch, LEVELSRECORD* plevelsrec,
                               int32 level)
{
  FORM**        forms;

  HQASSERT((pch != NULL),
           "ht_allocatechentry: NULL pointer to ht screen");
  HQASSERT(pch->formclass != NULL, "Allocating form for pattern screen");

  if ( pch->formclass != NULL ) {
    forms = theICachedForms(plevelsrec);
    HQASSERT( forms[level] == NULL, "Reallocating form" );

    if ( plevelsrec->erasenr != input_dl || deferring_allocation == 0 ) {
      /* Gotta allocate forms now */
      context->between_operators = context->is_interpreter;
      /* This can be called from the renderer (backdrop to page conversion). */
      forms[level] = make_ht_form(theIHalfMXDims(pch),
                                  theIHalfMYDims(pch),
                                  pch->formclass->formsize,
                                  mm_cost_below_reserves);
      /** \todo The actual cost of halftone forms should be
          determined. For now, equivalent to old code. */
      context->between_operators = FALSE;
      if ( forms[level] == NULL ) {
        /* Failed to allocate form.  Do not mark halftones as invalid after
           preload has already been called. */
        if ( !plevelsrec->preloaded ) {
          forms[level] = INVALID_FORM;
          ++plevelsrec->levels_used;
        } else if ( !preload_failed ) {
          monitorf(UVS("Warning: Ran out of space for halftone screen caching. Performance may be affected.\n")) ;
          preload_failed = TRUE;
        }
      } else { /* Got new form - increase number cached */
        ++theINumberCached(plevelsrec);
        ++plevelsrec->levels_used;
      }

    } else { /* Just mark form for later allocation */
      HQASSERT(!plevelsrec->preloaded, "Deferring screen after preload");
      forms[level] = DEFERRED_FORM;
      ++pch->num_deferred; ++gNumDeferred;
      ++plevelsrec->levels_used;
    }
  }
} /* Function ht_allocatechentry */


/* Test if screens match in all fields that affect rendering,
   cf. init_form().  ch2 is adjusted by the depth_shift given.  If
   adjustableRatio is true, don't compare fields that only affect choice
   of cell to render, not dot positions (for callers that will then
   modify them to match). */
Bool ht_equivalent_render_params(CHALFTONE *ch1, CHALFTONE *ch2,
                                 uint8 depth_shift, SYSTEMVALUE orientation,
                                 Bool adjustableRatio)
{
  int32 r1, r2, r3, r4;
  Bool xyswap = FALSE;

  HQASSERT(ch2->depth_shift == 0 || depth_shift == 0,
           "No arbitrary depth adjustment yet");
  HQASSERT(ch2->maxthxfer == 0 || fmod(orientation, 90.0) == 0.0,
           "Invalid orientation");

  if ( ch2->maxthxfer != 0 ) {
    if ( ch1->oangle != orientation )
      return FALSE;
    xyswap = fmod(fabs(ch2->oangle - orientation), 180.0) == 90.0;
  }
  if ( xyswap ) {
    r1 = ch2->halfr3 << depth_shift; r3 = ch2->halfr1;
    r2 = ch2->halfr4; r4 = ch2->halfr2 << depth_shift;
  } else {
    r1 = ch2->halfr1 << depth_shift; r3 = ch2->halfr3;
    r2 = ch2->halfr2; r4 = ch2->halfr4 << depth_shift;
  }
  /* Some of these tests will be redundant, but never mind. */
  return ch1->halfr1 == r1 && ch1->halfr2 == r2
    && ch1->halfr3 == r3 && ch1->halfr4 == r4
    && ch1->accurateScreen == ch2->accurateScreen
    && HalfGetDotCentered(ch1) == HalfGetDotCentered(ch2)
    && HalfGetMultithreshold(ch1) == HalfGetMultithreshold(ch2)
    && ch1->hpstwo == ch2->hpstwo
    && ch1->supcell_multiplesize == ch2->supcell_multiplesize
    && ch1->supcell_actual == ch2->supcell_actual
    && ch1->depth_shift == ch2->depth_shift + depth_shift
    && ch1->maxthxfer == ch2->maxthxfer
    && (adjustableRatio
        || (HalfGetExtraGrays(ch1) == HalfGetExtraGrays(ch2)
            && ch1->supcell_remainder == ch2->supcell_remainder
            && fabs(ch1->supcell_ratio - ch2->supcell_ratio) < EPSILON
            && ch1->notones == ch2->notones));
}

/* ht_equivalentchentry()

 * This function checks to see if an equivalent screen (same dot
 * patterns) has already been defined.  The actual dot coordinates are
 * compared, and this is half of what distinguishes this function from
 * ht_equivalent_ch_pre_cacheentry().  The other half is that this
 * doesn't look in the disk cache (the callers have already done that).
 *
 * Doesn't try to find equivalence with pattern screens, which don't
 * have an xcoords or ycoords array, so it doesn't make much sense to
 * try. Instead, skip over them to the next halftone. */

static Bool ht_equivalentchentry(
  CHALFTONE *chptr,
  HTTYPE objtype,
  NAMECACHE *sfcolor,
  Bool cacheable,
  HTTYPE cacheType,
  SPOTNO *oldspotno,
  HTTYPE *oldtype,
  COLORANTINDEX *oldci )
{
  register int32 j;
  register int16 *mxcoords , *mycoords ;
  register uint32 *mythxfer, *thxfer;
  CHALFTONE *c_chptr ;
  LISTCHALFTONE *listchptr ;
  ht_cacheIterator iter;
  SPOTNO c_spotno;
  COLORANTINDEX c_ci;
  HTTYPE c_type;
  int32 supcell_actual;
  uint16 maxthxfer;
  int16 *xcoords, *ycoords;

  UNUSED_PARAM(HTTYPE, objtype); /* Only for asserting */
  UNUSED_PARAM(NAMECACHE*, sfcolor); /* Only for asserting */
  UNUSED_PARAM(Bool, cacheable); /* Only for asserting */
  UNUSED_PARAM(Bool, cacheType); /* Only for asserting */
  HQASSERT(chptr != NULL, "chptr NULL in ht_equivalentchentry");
  thxfer = chptr->thxfer; maxthxfer = chptr->maxthxfer;
  HQASSERT((thxfer != NULL && maxthxfer != 0) ||
           (thxfer == NULL && maxthxfer == 0),
           "equivchentry: Threshold pointer and size out of step");
  xcoords = chptr->xcoords; ycoords = chptr->ycoords;
  HQASSERT((xcoords != NULL) && (ycoords != NULL),
           "ht_equivalentchentry expects screens with coord arrays");

  supcell_actual = chptr->supcell_actual;
  ht_iterChentryBegin(&iter, INVALID_DL /* search the whole cache */,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listchptr, &c_chptr, NULL,
                             &c_spotno, &c_type, &c_ci) ) {
    if ( c_chptr == NULL )
      continue; /* Can't match modular screen */

    mxcoords = theIXCoords(c_chptr);
    mycoords = theIYCoords(c_chptr);
    mythxfer = theITHXfer(c_chptr);

    if ( (chptr->sfname == NULL || chptr->sfname == c_chptr->sfname)
         && ht_equivalent_render_params(c_chptr, chptr, 0, chptr->oangle, FALSE)
         && ((thxfer && mythxfer) || (!thxfer && !mythxfer))
         && mxcoords && mycoords ) {

      /* Check the spot x/y coords too */
      for ( j = 0 ; j < supcell_actual ; ++j ) {
        if ( mxcoords[ j ] != xcoords[ j ] )
          break ;
        if ( mycoords[ j ] != ycoords[ j ] )
          break ;
      }

      if ( j == supcell_actual ) { /* coords matched */

        if ( thxfer ) {

          for ( j = 0 ; j <= maxthxfer ; ++j ) {
            if ( mythxfer[j] != thxfer[j] ) {
              break;
            }
          }
          if ( j == maxthxfer + 1 ) {
            *oldspotno = c_spotno; *oldci = c_ci; *oldtype = c_type;
            ht_iterChentryEnd(&iter);
            return TRUE;
          }

        } else {
          *oldspotno = c_spotno; *oldci = c_ci; *oldtype = c_type;
          ht_iterChentryEnd(&iter);
          return TRUE;
        }
      }
      HQASSERT(!cacheable || chptr->sfname != c_chptr->sfname
               || !( cacheType == HTTYPE_ALL
                     || c_type == cacheType || c_type == objtype )
               || ht_listchSFColor(listchptr) != sfcolor,
               /* ht_equivalent_ch_pre_cacheentry insists on these fields
                  matching in addition to the same conditions as here. */
               "Nonequivalent screen would pre-match!");
    }
  }
  ht_iterChentryEnd(&iter);
  return FALSE;
} /* Function ht_equivalentchentry */


/** Erase nr for the cache in \c ht_anyInRIPHalftonesUsed().  */
static dl_erase_nr in_rip_ht_used_last_dl = INVALID_DL;


Bool ht_anyInRIPHalftonesUsed(dl_erase_nr erasenr)
{
  static Bool last_used_cache = FALSE; /* cache of last result */
  ht_cacheIterator iter;
  CHALFTONE *chptr;

  if ( erasenr == in_rip_ht_used_last_dl )
    return last_used_cache;
  /* Search the ht cache to find the answer for this DL. */
  in_rip_ht_used_last_dl = erasenr; last_used_cache = FALSE;
  ht_iterChentryBegin(&iter, erasenr, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, NULL, &chptr, NULL, NULL, NULL, NULL) )
    if ( chptr != NULL && chptr->formclass != NULL ) {
      LEVELSRECORD *rec = chalftone_levels(chptr, erasenr);
      if ( rec != NULL && rec->levels_used != 0 ) {
        last_used_cache = TRUE; break;
      }
    }
  ht_iterChentryEnd(&iter);
  return last_used_cache;
}


/*
 *  Checks to see if a given (new) screen is already known either in the
 *  in-memory cache or from the disk cache.  If the latter, then the disk
 *  entry is loaded and copied into memory (all within loadHalftoneFromDisk()).
 *  This function is called _before_ the individual spot coordinates have
 *  been calculated specifically to save the RIP from having to do all
 *  that working out.  Instead, a new screen is matched by various
 *  parameters except the spot coords, principally chptr->sfname (which
 *  is actually the halftone name in threshold screens) and the sfcolor
 *  (could have different thresholds for two colors, even if all other
 *  params match), but also all the metrics, since resolution, frequency
 *  and angle can change the screen while sfname and sfcolor remain the
 *  same.
 *
 *  Returns 0 if none found, -1 on error, 1 on success.
 */
int ht_equivalent_ch_pre_cacheentry(
  corecontext_t *context,
  SPOTNO        spotno,
  HTTYPE        objtype,
  COLORANTINDEX ci,
  CHALFTONE*    chptr,
  uint8         depth_shift,
  uint8         default_depth_shift,
  SYSTEMVALUE   orientation,
  NAMECACHE*    htname,
  NAMECACHE*    sfcolor,
  NAMECACHE*    alternativeName,
  NAMECACHE*    alternativeColor,
  HTTYPE        cacheType,
  int32         detail_name,
  int32         detail_index,
  int32         phasex,
  int32         phasey)
{
  CHALFTONE *c_chptr;
  LISTCHALFTONE *listchptr ;
  ht_cacheIterator iter;
  SPOTNO c_spotno;
  COLORANTINDEX c_ci;
  HTTYPE c_type;
  int res = 0;

  HQASSERT(chptr != NULL, "no chptr in ht_equivalent_ch_pre_cacheentry");
  /* The logic of shift adjustment must match ht_insertchentry */
  if ( depth_shift == 0 )
    /* 1-bit screens are scaled to current depth */
    depth_shift = default_depth_shift;
  /* The logic of DotCentered must match ht_insertchentry */
  HalfSetDotCentered(chptr,
                     chptr->accurateScreen && context->systemparams->ScreenDotCentered);

  ht_iterChentryBegin(&iter, INVALID_DL /* search the whole cache */,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listchptr, &c_chptr, NULL,
                             &c_spotno, &c_type, &c_ci) ) {
    if ( c_chptr != NULL
         /* Assuming these three names determine the dot placement algorithm */
         && c_chptr->sfname == chptr->sfname
         && ( cacheType == HTTYPE_ALL
              || c_type == cacheType || c_type == objtype )
         && ht_listchSFColor(listchptr) == sfcolor
         /* compare metrics affected by gstate and affecting dot placement */
         && ht_equivalent_render_params(c_chptr, chptr,
                                        depth_shift, orientation, FALSE) ) {
      res = ( ht_duplicatechentry(spotno, objtype, ci, sfcolor, htname,
                                   c_spotno, c_type, c_ci, phasex, phasey)
              ? 1 : -1);
      ht_iterChentryEnd(&iter);
      return res;
    }
  }
  ht_iterChentryEnd(&iter);
  if ( (chptr->sfname != NULL || alternativeName != NULL) &&
       (chptr->accurateScreen ||
        (sfcolor != NULL || alternativeColor != NULL) ||
        (detail_name == NAME_HDS)) )
    res = loadHalftoneFromDisk( context, spotno, objtype, ci, chptr,
                                depth_shift, default_depth_shift,
                                orientation,
                                htname, sfcolor,
                                alternativeName, alternativeColor, cacheType,
                                detail_name, detail_index,
                                phasex, phasey );
  return res;
} /* Function ht_equivalent_ch_pre_cacheentry */


static Bool ht_equivalentchpatentry(
  LISTCHALFTONE*  newlistchptr,
  CHALFTONE *newchptr,
  SPOTNO* oldspotno,
  COLORANTINDEX* oldci,
  HTTYPE *oldtype )
{
  register int32 j;
  register int32 r1 , r2 , r3 , r4  ;

  register uint8 *nbits , *obits ;
  LISTCHALFTONE *oldlistchptr;
  CHALFTONE *oldchptr;
  ht_cacheIterator iter;
  SPOTNO c_spotno;
  COLORANTINDEX c_ci;
  HTTYPE c_type;
  FORM * form;

  r1 = theIHalfR1( newchptr ) ;
  r2 = theIHalfR2( newchptr ) ;
  r3 = theIHalfR3( newchptr ) ;
  r4 = theIHalfR4( newchptr ) ;

  ht_iterChentryBegin(&iter, INVALID_DL /* search the whole cache */,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &oldlistchptr, &oldchptr, NULL,
                             &c_spotno, &c_type, &c_ci) ) {
    if ( oldlistchptr != newlistchptr ) {
      if ( oldchptr != NULL && oldchptr->formclass == NULL &&
           /* it really is a pattern screen */
           (theIHalfR1(oldchptr) == r1) &&
           (theIHalfR2(oldchptr) == r2) &&
           (theIHalfR3(oldchptr) == r3) &&
           (theIHalfR4(oldchptr) == r4) ) {
        form = oldchptr->pattern_form;
        if ( NULL == form ) {
          form = theICachedForms(theIUsageLevels(oldchptr))[1];
        }
        obits = (uint8*)theFormA(*form);

        form = newchptr->pattern_form;
        if ( NULL == form ) {
          form = theICachedForms(theIUsageLevels(newchptr))[1];
        }
        nbits = (uint8*)theFormA(*form);

        /* examine the used area, may be smaller than IFormS[ize] */
        for ( j = theFormL(*form)*theFormH(*form); j > 0 ; --j ) {
          if ( (int32)(*nbits++) != (int32)(*obits++) ) {
            break ;
          }
        }
        if ( ! j ) {
          *oldspotno = c_spotno; *oldci = c_ci; *oldtype = c_type;
          ht_iterChentryEnd(&iter);
          return TRUE;
        }
      }
    }
  }
  ht_iterChentryEnd(&iter);
  return FALSE;
} /* Function ht_equivalentchpatentry */


#define SWAP(type, a, b) MACRO_START \
  type _tmp = (a); (a) = (b); (b) = _tmp; \
MACRO_END


/* Reflects coordinates to increase the other way */
static void reflect_coords(int16 *coords, int32 dim, size_t count)
{
  size_t i;
  int16 max = (int16)dim - 1;

  for ( i = 0 ; i < count ; ++i )
    coords[i] = max - coords[i];
}


static void rotate_threshold_screen(CHALFTONE *ch, SYSTEMVALUE orientation)
{
  SYSTEMVALUE angle = orientation - ch->oangle;

  if ( angle == 90.0 || angle == -270.0 ) { /* x -> -y, y -> x */
    SWAP(int32, ch->halfxdims, ch->halfydims);
    SWAP(int32, ch->halfr1, ch->halfr3);
    SWAP(int32, ch->halfr2, ch->halfr4);
    SWAP(int16*, ch->xcoords, ch->ycoords);
    reflect_coords(ch->ycoords, ch->halfydims, (size_t)ch->supcell_actual);
  } else if ( angle == 180.0 || angle == -180.0 ) { /* x -> -x, y -> -y */
    reflect_coords(ch->xcoords, ch->halfxdims, (size_t)ch->supcell_actual);
    reflect_coords(ch->ycoords, ch->halfydims, (size_t)ch->supcell_actual);
  } else if ( angle == 270.0 || angle == -90.0 ) { /* x -> y, y -> -x */
    SWAP(int32, ch->halfxdims, ch->halfydims);
    SWAP(int32, ch->halfr1, ch->halfr3);
    SWAP(int32, ch->halfr2, ch->halfr4);
    SWAP(int16*, ch->xcoords, ch->ycoords);
    reflect_coords(ch->xcoords, ch->halfxdims, (size_t)ch->supcell_actual);
  } else
    HQASSERT(angle == 0.0, "Bogus rotation angle");
  ch->oangle = orientation;
}


static int32 decide_ht_type(
  int32         r1,
  int32         r2,
  int32         r3,
  int32         r4,
  int32         xdims,
  int32         ydims )
{
  if (( r1 == 0  && r3 == 0 ) || ( r2 == 0  && r4 == 0 ) ||
      ( r2 == r3  && r1 == r4 )) {
    if ((xdims ==  1 || xdims ==  2 || xdims ==  4 ||
         xdims ==  8 || xdims == 16 || xdims == 32
#if BLIT_WIDTH_BYTES > 4
         || xdims == 64
#endif
         ) &&
        (ydims ==  1 || ydims ==  2 || ydims ==  4 ||
         ydims ==  8 || ydims == 16 || ydims == 32
#if BLIT_WIDTH_BYTES > 4
         || ydims == 64
#endif
         ))
      return SPECIAL;
    else
      if ( xdims < BLIT_WIDTH_BITS )
        return ONELESSWORD;
      else
        return ORTHOGONAL;
  }
  return GENERAL;
}


/** Inserts a new set of screens into the display list's hash table.
   All the paramaters currently passed across may eventually assume
   global relevance. */
Bool ht_insertchentry(
  corecontext_t *context,
  SPOTNO        spotno,
  HTTYPE        objtype,
  COLORANTINDEX ci,
  CHALFTONE*    chptr,
  uint8         depth_shift,
  uint8         default_depth_shift,
  Bool          needs_depth_adjustment,
  SYSTEMVALUE   orientation,
  int32         patterngraylevel,
  NAMECACHE*    htname,
  NAMECACHE*    sfcolor,
  int32         detail_name,
  int32         detail_index,
  Bool          cachetodisk,
  NAMECACHE *   alternativeName,
  NAMECACHE *   alternativeColor,
  HTTYPE        cacheType,
  int32         phasex,
  int32         phasey )
{
  SYSTEMPARAMS *systemparams = context->systemparams;
  register int32 tmp ;
  register int32 i;
  int32 httype;
  int32 dly , sly ;
  int32 exdims , eydims ;
  int32 mxdims , mydims ;
  SPOTNO oldspotno = 0;
  COLORANTINDEX oldci = 0;
  HTTYPE oldtype = 0;
  FORM *form = NULL ;
  size_t formsize;
  FORMCLASS *formclass = NULL;
  LISTCHALFTONE* listchptr = NULL;
  LEVELSRECORD* levelsrec = NULL;
  int32* ys = NULL; /* reassure compiler */
  int32 r1, r2, r3, r4;
  int32 xdims, ydims;
  int32 actionNumber = 0;
  Bool cacheable;
  Bool created_formclass = FALSE;

  HQTRACE(debug_halftonecache, ("ht_insertchentry: %d %d", spotno, ci));
  HQASSERT(chptr != NULL, "insertchentry: NULL 'new' arguments");
  HQASSERT((chptr->thxfer != NULL && chptr->maxthxfer != 0) ||
           (chptr->thxfer == NULL && chptr->maxthxfer == 0),
           "insertchentry: Threshold pointer and size out of step");

  if ( chptr->thxfer != NULL && chptr->oangle != orientation )
    rotate_threshold_screen(chptr, orientation);

  /* The logic of shift adjustment must match ht_equivalent_ch_pre_cacheentry */
  if ( depth_shift == 0 )
    /* 1-bit screens are scaled to current depth */
    depth_shift = default_depth_shift;
  chptr->depth_shift = depth_shift;
  if ( depth_shift != 0 && needs_depth_adjustment ) {
    /* Adjust all the x coordinates */
    chptr->halfxdims <<= depth_shift;
    chptr->halfr1 <<= depth_shift; chptr->halfr4 <<= depth_shift;
    for (i = 0 ; i < chptr->supcell_actual ; ++i )
      chptr->xcoords[i] <<= depth_shift;
  }

  HalfSetDotCentered(chptr,
                     chptr->accurateScreen && systemparams->ScreenDotCentered);

  if (alternativeName == NULL)
    alternativeName = chptr->sfname;
  if (alternativeColor == NULL)
    alternativeColor = sfcolor;
  cacheable = cachetodisk && alternativeName != NULL
    && (chptr->accurateScreen || alternativeColor != NULL || detail_name == NAME_HDS);

  if ( patterngraylevel < 0 ) {
    multi_mutex_lock(&ht_cache_mutex);
    /* Lock so that any spot found is not purged before duplicated. */
    if ( ht_equivalentchentry(chptr, objtype, sfcolor, cacheable, cacheType,
                              &oldspotno, &oldtype, &oldci) ) {
      Bool res = TRUE;

      freeqhalftone(chptr, listchptr);
      if ( !ht_duplicatechentry(spotno, objtype, ci, sfcolor, htname,
                                oldspotno, oldtype, oldci, phasex, phasey) ) {
        multi_mutex_unlock(&ht_cache_mutex);
        return FALSE;
      }
      chptr = ht_getch(spotno, objtype, ci);
      HQASSERT(chptr != NULL,
               "ht_insertchentry: duplicated ch not found to save to disk");
      if ( cacheable )
        res = saveHalftoneToDisk(context, chptr,
                                 alternativeName, alternativeColor, cacheType,
                                 detail_name, detail_index);
      multi_mutex_unlock(&ht_cache_mutex);
      return res;
    }
    multi_mutex_unlock(&ht_cache_mutex);
  } else { /* A pattern screen */
    chptr->notones = 2;
  }

  /* Fill in easy entries. */
  theIDuplicate( chptr ) = 1 ;
  theIScreenMarked( chptr ) = FALSE ;
  theIScreenReport( chptr ) = FALSE;
  chptr->num_deferred = 0;
  chptr->lock = 0;

  /* Zero spare fields - this means content is known if the struct changes and
     we use one of these fields - may be able to avoid changing the version
     number. Note that pointer spares are nulled by disk caching. */
  chptr->spare1 = 0; chptr->spare2 = 0; chptr->spare3 = 0; chptr->spare5 = 0;

  HQASSERT( spotno > 0 && spotno <= 0xFFFF ,
            "ht_insertchentry: Unexpected spotno when deriving screen index" );
  HQASSERT( ci == COLORANTINDEX_NONE || ci >= 0 ,
            "ht_insertchentry: Unexpected colorant index when deriving screen index" );
  theIScreenIndex(chptr) = (objtype << 26) | ((ci & 0x3FF) << 16) | spotno;

  r1 = chptr->halfr1; r2 = chptr->halfr2; r3 = chptr->halfr3; r4 = chptr->halfr4;
  xdims = chptr->halfxdims; ydims = chptr->halfydims;
  httype = decide_ht_type(r1, r2, r3, r4, xdims, ydims);
  chptr->halftype = httype;
  switch ( httype ) {
  case SPECIAL :
    exdims = xdims ;
    eydims = ydims ;
    mxdims = BLIT_WIDTH_BITS ;
    mydims = BLIT_WIDTH_BITS ;
    break ;

  case ONELESSWORD :
    exdims = xdims ;
    eydims = ydims ;
    mxdims = BLIT_WIDTH_BITS ;
    mydims = ydims ;
    theIHalfRotate( chptr ) = BLIT_WIDTH_BITS % xdims;
    break ;

  case ORTHOGONAL :
    exdims = xdims ;
    eydims = ydims ;
    mxdims = ((xdims + BLIT_WIDTH_BITS + BLIT_MASK_BITS) & ~BLIT_MASK_BITS) ;
    mydims = ydims ;
    break ;

  case GENERAL :                                /* Includes FASTGENERAL */
  case SLOWGENERAL :                            /* Includes FASTGENERAL */
    r1 = abs( r1 ) ;
    r2 = abs( r2 ) ;
    r3 = abs( r3 ) ;
    r4 = abs( r4 ) ;
    tmp = ( r1 * r3 ) + ( r2 * r4 ) ;
    exdims = tmp / gcd( r2 , r3 ) ;
    eydims = tmp / gcd( r1 , r4 ) ;
    if ( xdims > BLIT_WIDTH_BITS * 2 ) { /* TWO WORDS WIDE FOR NOW + ONE EXTRA */
      theIHalfType( chptr ) = SLOWGENERAL ;
      mxdims = ((xdims + BLIT_WIDTH_BITS + BLIT_MASK_BITS) & ~BLIT_MASK_BITS) ;
      mydims = ydims ;

    } else {
      mxdims = ((exdims + BLIT_WIDTH_BITS + BLIT_MASK_BITS) & ~BLIT_MASK_BITS) ;
      mydims = ydims ;
    }
    mydims -= min(r2, r3) ;
    break;

  default:
    HQFAIL("unknown halftone type in ht_insertchentry");
    return FALSE  ; /* probably needed to keep the compiler/lint happy */
  }

  theIHalfEXDims( chptr ) = exdims ;
  theIHalfEYDims( chptr ) = eydims ;
  theIHalfMXDims( chptr ) = mxdims ;
  theIHalfMYDims( chptr ) = mydims ;

  /* Calculate the byte repeat length. This takes into account the
     implementation of halftone convergence replication in converge.c, which
     requires at least two blit words worth of source data. The repeatb
     parameter is rounded up to the nearest word width before filling in bits
     from the screen data, so if repeatb is strictly greater than one word
     wide, we will force generation of two blit words before copying. */
  tmp = exdims ;
  while ( (tmp & 7) != 0 || tmp <= BLIT_WIDTH_BITS ) {
    tmp += exdims ;
  }
  theIHalfRepeatB( chptr ) = tmp >> 3 ;

  listchptr = ht_listchAlloc(spotno, objtype, ci, chptr,
                             sfcolor, htname, NULL, phasex, phasey);
  if ( listchptr == NULL ) {
    freeqhalftone(chptr, listchptr);
    return FALSE;
  }

  /* Set up formclass */
  formsize = FORM_LINE_BYTES(mxdims) * mydims;
  HQASSERT( mxdims != 0,
            "ht_insertchentry: Got zero width" );
  HQASSERT( mydims != 0,
            "ht_insertchentry: Got zero height" );
  HQASSERT( formsize != 0,
            "ht_insertchentry: Got formsize of zero" );

  if ( patterngraylevel >= 0 ) {
    chptr->formclass = NULL;
  } else {
    /* A normal screen.
     * Find the formclass into which the form will fit best
     */
    multi_mutex_lock(&formclasses_mutex);
    formclass = formclasses;
    while ( (formclass != NULL) && (formclass->formsize != formsize) ) {
      formclass = formclass->next_formclass;
    }
    if ( formclass == NULL ) { /* create a new form class */
      formclass = alloc_formclass(formsize);
      if ( formclass == NULL ) {
        freeqhalftone(chptr, listchptr);
        return FALSE;
      }
      created_formclass = TRUE;
      /* If insert fails, this formclass is not removed, but that's harmless */
    }
    /* put the chptr into the form class */
    chptr->formclass = formclass; ++formclass->num_screens;
    multi_mutex_unlock(&formclasses_mutex);
  }

  do {
    if ( (levelsrec = levelsrecs_alloc(context, chptr->notones)) != NULL
         && (ys = (int32 *)mm_alloc_with_header(mm_pool_temp,
                                                mydims * sizeof(int32),
                                                MM_ALLOC_CLASS_HALFTONE_YS))
            != NULL
         /* Allocate one form now so that we don't get stuck in
          * preloadchforms if there is no memory left */
         && ( !(patterngraylevel >= 0 || created_formclass)
              || (form = make_ht_form(mxdims, mydims, formsize, mm_cost_normal))
                 != NULL ) )
      break; /* success */
    /* free before low-mem actions */
    if ( levelsrec != NULL ) {
      levelsrecs_free(levelsrec, chptr->notones); levelsrec = NULL;
      if ( ys != NULL) {
        mm_free_with_header(mm_pool_temp, ys); ys = NULL;
      }
    }
    HQTRACE( debug_lowmemory, ( "CALL(handleLowMemory): insertchentry" ));
    actionNumber = handleLowMemory( actionNumber, TRY_MOST_METHODS, NULL );
  } while ( actionNumber > 0 );
  if ( ys == NULL ) { /* must have failed */
    freeqhalftone(chptr, listchptr);
    if ( actionNumber < 0 ) /* error in low-mem handler */
      return FALSE;
    else
      return error_handler(VMERROR);
  }

  levelsrecs_init(levelsrec, chptr->notones, input_dl);
  chptr->usage = chptr->levels = levelsrec; chptr->forms = NULL;

  chptr->halfys = ys;
  /* Set the increments for each line of the halftone. */
  sly = 0 ;
  ys[ 0 ] = 0 ;
  dly = FORM_LINE_BYTES(mxdims) ;
  for ( i = 1 ; i < mydims ; ++i ) {
    sly += dly ;
    ys[ i ] = sly ;
  }

  if ( patterngraylevel >= 0 ) {
    chptr->pattern_form = form;
  } else if ( created_formclass ) {
    /* Since it was just created, this is the only thread manipulating it. */
    formclass->form_chain = form ;
    ((HTFORM *)form)->nextf = NULL ;
    formclass->num_forms = 1;
  }

  /* Record current password permissions. */
  HalfSetLowResHDS(chptr,
                   (chptr->screenprotection & SCREENPROT_PASSREQ_MASK)
                     == SCREENPROT_PASSREQ_HDS
                   && systemparams->HDS == PARAMS_HDS_LOWRES);
  HalfSetLowResHXM(chptr,
                   (chptr->screenprotection & SCREENPROT_PASSREQ_MASK)
                     == SCREENPROT_PASSREQ_HXM
                   && systemparams->HXM == PARAMS_HXM_LOWRES);
  /* Check if optimization error exceeds current limits. */
  HalfSetOptimizationErrorExceeded(chptr,
    chptr->efreq > systemparams->ScreenFrequencyAccuracy
    || chptr->eangle > systemparams->ScreenAngleAccuracy);

  /* NULL for safety. */
  chptr->prev_chptr = NULL;
  chptr->next_chptr = NULL;

  if ( patterngraylevel >= 0 ) {
    ht_params_t htp;

    chptr->forms = levelsrec;
    rawstorechglobals(&htp, chptr);
    area0fill(form);
    set_cell_bits(form, &htp,
                  chptr->xcoords + patterngraylevel,
                  chptr->ycoords + patterngraylevel,
                  theISuperCellActual(chptr) - patterngraylevel,
                  r1, r2, r3, r4,
                  ys, 1, chptr->supcell_multiplesize,
                  HalfGetDotCentered(chptr), chptr->accurateScreen,
                  theITHXfer( chptr ) != NULL ) ;
    bitexpandform(&htp, mxdims, mydims, form);

    mm_free_with_header(mm_pool_temp, chptr->xcoords);
    theIXCoords(chptr) = NULL;
    theIYCoords(chptr) = NULL;

    multi_mutex_lock(&ht_cache_mutex);
    if ( ht_equivalentchpatentry(listchptr, chptr,
                                 &oldspotno, &oldci, &oldtype) ) {
      Bool res;

      res = ht_duplicatechentry(spotno, objtype, ci, system_names+NAME_Pattern,
                                htname, oldspotno, oldtype, oldci,
                                phasex, phasey);
      multi_mutex_unlock(&ht_cache_mutex);
      return res;
    }
    multi_mutex_unlock(&ht_cache_mutex);
  }

  ht_cacheInsert(listchptr);

  if ( cacheable && patterngraylevel < 0 )
    if ( !saveHalftoneToDisk(context, chptr,
                             alternativeName, alternativeColor, cacheType,
                             detail_name, detail_index) )
      return FALSE;
  return TRUE;
} /* Function ht_insertchentry */


/** Returns number of forms short of the max required for this form class. */
static size_t formclass_deficit(FORMCLASS* formclass)
{
  size_t total_forms = formclass->num_forms
    + (ht_form_keep ? formclass->levels_cached : 0);
  /* Can't rely on cached forms, since they belong to a previous sheet. Except
     with ht_form_keep, since then they will not be dealloced. */

  HQASSERT(ht_form_keep || total_forms <= formclass->levels_reqd + 1,
           /* Allow the one default form over the limit. */
           "Too many forms in class");
  return formclass->levels_reqd < total_forms
         ? 0 : formclass->levels_reqd - total_forms;
}


/* ---------------------------------------------------------------------- */
static void clear_screen_mark(dl_erase_nr dl)
{
  ht_cacheIterator iter;
  CHALFTONE *chptr ;
  MODHTONE_REF *mhtref;

  ht_iterChentryBegin(&iter, dl, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, NULL, &chptr, &mhtref, NULL, NULL, NULL) )
    if ( chptr != NULL )
      theIScreenMarked(chptr) = FALSE;
    else
      mhtref->screenmark = FALSE;
  ht_iterChentryEnd(&iter);
}


/** Loads the form caches of the halftones according to their usage.
 */
static Bool ht_preloadchforms(corecontext_t *context,
                              dl_erase_nr erasenr, GUCR_CHANNEL* hf)
{
  DL_STATE *page = context->page;
  size_t total_mem_reqd = 0;
  size_t total_mem_used = 0;
  FORM**                forms;
  FORMCLASS*            formclass;
  LISTCHALFTONE*        listchptr;
  LEVELSRECORD*         levelsrec;
  CHALFTONE*            chptr;
  GUCR_COLORANT*        hc;
  GUCR_CHANNEL*         hfForAll;
  ht_cacheIterator      iter;
  size_t i;
  size_t failed_size;
  size_t largest_deficit, second_largest_deficit;

  /* Reset formclass chains and counters */
  multi_mutex_lock(&formclasses_mutex);
  for ( formclass = formclasses ; formclass != NULL; formclass = formclass->next_formclass ) {
    /* These fields are only used for rendering, and this is guaranteed to be
       the only thread rendering. */
    formclass->levels_reqd     = 0;
    formclass->levels_cached   = 0;
    formclass->lru_chptr       = NULL;
    formclass->mru_chptr       = NULL;
    formclass->num_screens_sheet = 0;
  }
  multi_mutex_unlock(&formclasses_mutex);

  /* Collect info about forms into formclasses and reset marks on levels. */

  /* Loop over all colorants of all frames in this sheet */
  clear_screen_mark(erasenr);
  if ( gucr_framesMore(hf) ) {
    for ( hfForAll = hf ; ; ) {
      for ( hc = gucr_colorantsStart(hfForAll);
            gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
            gucr_colorantsNext(&hc) ) {
        const GUCR_COLORANT_INFO *colorantInfo;

        if ( gucr_colorantDescription(hc, &colorantInfo) ) {
          COLORANTINDEX ci = colorantInfo->colorantIndex ;

          /* Loop over all cache entries for this colorant (or default) */
          ht_iterChentryBegin(&iter, erasenr, SPOT_NO_INVALID, ci);
          while ( ht_iterChentryNext(&iter, &listchptr, &chptr, NULL,
                                     NULL, NULL, NULL) ) {
            if ( chptr == NULL /* modular */
                 || chptr->notones < 2 || chptr->screenmark )
              continue;
            chptr->screenmark = TRUE;
            levelsrec = chalftone_output_levels(chptr);
            if ( levelsrec == NULL ) /* wasn't used */ {
              chptr->forms = NULL; continue;
            }
            levelsrec->preloaded = TRUE;
            {
              formclass = chptr->formclass;
              if ( formclass == NULL ) { /* a pattern screen */
                if ( chptr->pattern_form != NULL ) {
                  levelsrec->levels[1] = chptr->pattern_form;
                  chptr->pattern_form = NULL;
                }
                levelsrec->number_cached = 1;
              } else { /* normal screen */
                HT_LOCK_WR_CLAIM( formclass );
                formclass->erasenr = erasenr; /* take ownership */
                formclass->levels_reqd += levelsrec->levels_used;
                formclass->levels_cached += levelsrec->number_cached;
                formclass->num_screens_sheet++;
                total_mem_reqd += formclass->formsize * levelsrec->levels_used;
                total_mem_used += formclass->formsize *
                  (levelsrec->number_cached
                   /* Add forms in the formclass chain, but only once. */
                   + (formclass->num_screens_sheet == 1
                      ? formclass->num_forms : 0));

                /* link in the chptr into the MRU-LRU chain */
                chptr->next_chptr = formclass->mru_chptr;
                formclass->mru_chptr = chptr;
                if ( chptr->next_chptr != NULL )
                  chptr->next_chptr->prev_chptr = chptr ;
                chptr->prev_chptr = NULL ;
                if ( formclass->lru_chptr == NULL )
                  formclass->lru_chptr = chptr;

                /* loop over levels, move forms to formclass, clear markers */
                forms = levelsrec->levels;
                for ( i = chptr->notones - 1 ; i > 0 ; --i ) {
                  if ( forms[i] != NULL ) {
#ifdef LEVEL_TRACKING
                    levelsrec->level_used[i] = TRUE;
#endif
                    HQASSERT(forms[i] != DEFERRED_FORM,
                             "Deferred form left until render");
                    if ( forms[i] == INVALID_FORM || forms[i] == DEFERRED_FORM )
                      forms[i] = NULL;
                    else if ( forms[i]->hoff != HT_FORM_INITED ) { /* move to formclass */
                      ((HTFORM*)forms[i])->nextf = (HTFORM*)formclass->form_chain;
                      formclass->form_chain = forms[i];
                      ++formclass->num_forms; --formclass->levels_cached;
                      --levelsrec->number_cached;
                      forms[i] = NULL;
                    }
                  }
                }
                HQASSERT(ht_form_keep || levelsrec->number_cached == 0,
                         "Forms miscounted");
                levelsrecord_check(levelsrec, chptr->notones);
                HT_LOCK_RELEASE();
              }
              /* It is just possible for someone to get through the
                 security for HDS and HCMS if they up the resolution
                 after having set up HDS. This is because the screen is
                 resolution-independent and therefore if they catch the
                 security error and continue, they'll be left with a
                 valid screen for the higher resolution. So catch that
                 case here before allowing the page to output. */
              if ( (HalfGetLowResHDS(chptr)
                    && !fSECAllowLowResHDS(page->ydpi))
                   || (HalfGetLowResHXM(chptr)
                       && !fSECAllowLowResHXM(page->xdpi, page->ydpi)) ) {
                ht_iterChentryEnd(&iter);
                return error_handler(INVALIDACCESS);
              }
            }
          } /* for all cache entries */
          ht_iterChentryEnd(&iter);
        }
      } /* for all colorants */

      gucr_framesNext(& hfForAll);
      if ( !gucr_framesMore(hfForAll) )
        break;
      if ( gucr_framesStartOfSheet(hfForAll, NULL, NULL) )
        break;
    } /* for all frames in sheet */
  } /* if (gucr_framesMore (hf)) */

  /* Allocate the required forms, evenly across the screens, in case
     memory runs out in the middle. For efficiency, "evenly" means
     equalizing the total size of missing forms in each screen. */
  failed_size = (size_t)-1;
  for (;;) {
    size_t size_to_alloc, size_alloced;
    FORMCLASS *largest_deficit_class = NULL;

    /* Find class with largest deficit. */
    largest_deficit = 0; second_largest_deficit = 0;
    multi_mutex_lock(&formclasses_mutex);
    for ( formclass = formclasses ; formclass != NULL
            ; formclass = formclass->next_formclass ) {
      size_t deficit = formclass_deficit(formclass);

      if ( deficit > 0 && formclass->formsize < failed_size ) {
        size_t deficit_size =
          deficit * formclass->formsize / formclass->num_screens_sheet;
        if ( deficit_size >= largest_deficit ) {
          second_largest_deficit = largest_deficit;
          largest_deficit = deficit_size; largest_deficit_class = formclass;
        }
      }
    }
    multi_mutex_unlock(&formclasses_mutex);
    /* Formclasses may be added during preload, but they'll have 0 deficit. */
    if ( largest_deficit == 0 )
      break; /* all possible allocs done */
    HT_LOCK_WR_CLAIM(largest_deficit_class);
    /* Allocate enough to equalize with the 2nd largest, or at least a tenth. */
    size_to_alloc = largest_deficit_class->num_screens_sheet
      * max(largest_deficit - second_largest_deficit, largest_deficit / 10);
    size_alloced = 0;
    do { /* allocate at least one form */
      /** \todo The real cost of halftone forms should be determined. */
      FORM *aform =
        make_ht_form( 0, 0, largest_deficit_class->formsize, mm_cost_normal );
      if ( aform == NULL ) {
        failed_size = largest_deficit_class->formsize;
        break;
      }
      ((HTFORM*)aform)->nextf = (HTFORM*)largest_deficit_class->form_chain;
      largest_deficit_class->form_chain = aform;
      ++largest_deficit_class->num_forms;
      size_alloced += largest_deficit_class->formsize;
      total_mem_used += largest_deficit_class->formsize;
    } while ( size_to_alloc > size_alloced );
    HT_LOCK_RELEASE();

    if ( !interrupts_clear(allow_interrupt) )
      return report_interrupt(allow_interrupt);
  }

  preload_failed = failed_size != (size_t)-1;
  if ( preload_failed ) {
    char buf[MAX_MM_PRETTY_MEMORY_STRING_LENGTH];
    monitorf(UVM("Warning: Only %2.0f%% of the %s needed for halftone screen caching was available. Performance may be affected.\n"),
      100.0 * total_mem_used / total_mem_reqd,
      mm_pretty_memory_string((uint32)(total_mem_reqd / 1024), buf));
  }
#if defined( ASSERT_BUILD )
  report_ch_caching(erasenr);
#endif
  return TRUE ;
} /* Function ht_preloadchforms */


/* ---------------------------------------------------------------------- */
/* Remove all the forms allocated to the halftone cache.  If fReset is
   true, reset them to INVALID_FORM, otherwise remove them permanently
   and clear the levels counts. */
static void unloadchforms(dl_erase_nr erasenr, Bool fReset)
{
  FORM *resetform = fReset ? INVALID_FORM : NULL;
  ht_cacheIterator iter;
  register int32 j;
  LISTCHALFTONE *listch;
  CHALFTONE *chptr;
  MODHTONE_REF *mhtref;
  FORM **forms;

  HQTRACE(debug_halftonecache,("Unloadchforms %s",resetform?"INV":"NULL"));
  debug_display_ht_table(erasenr);

  ht_iterChentryBegin(&iter, erasenr, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listch, &chptr, &mhtref,
                             NULL, NULL, NULL) ) {
    LEVELSRECORD *levelsrec ;
    FORMCLASS *formclass ;
    Bool grabform;

    if ( chptr == NULL ) { /* modular */
      if ( !fReset )
        htm_reset_used(mhtref, erasenr);
      continue;
    }
    if ( erasenr == output_dl )
      levelsrec = chalftone_output_levels(chptr); /* so chptr->forms is set */
    else
      levelsrec = chalftone_levels(chptr, erasenr);
    if ( levelsrec == NULL )
      continue; /* this screen was not used for this DL */
    levelsrec->preloaded = FALSE;
    if ( ht_form_keep ) {
      if ( !fReset )
        levelsrec->erasenr = INVALID_DL;
      continue;
    }
    formclass = chptr->formclass ;
    if ( ! formclass ) {
      /* a pattern screen */

      forms = theICachedForms( levelsrec ) ;
      if ( forms[1] ) {
        if ( forms[1] && forms[1] != INVALID_FORM &&
             forms[1] != DEFERRED_FORM )
          chptr->pattern_form = forms[1] ;
        forms[1] = resetform ;
        theINumberCached( levelsrec ) = 0 ;
      }
      if ( ! resetform ) {
        levelsrec->levels_used = 0; levelsrec->erasenr = INVALID_DL;
      }

    } else { /* a normal screen */

      if ( levelsrec->levels_used > 0 || levelsrec->number_cached > 0 ) {
        HT_LOCK_WR_CLAIM( formclass );

        grabform =
          /* If a new preload has started, it owns form_chain now. */
          formclass->form_chain == NULL && formclass->erasenr <= erasenr;
        forms = theICachedForms( levelsrec ) ;
        for ( j = theINoTones( chptr ) - 1 , ++forms ;
              j ;
              --j , ++forms ) {
          if ( *forms ) {
            if ( *forms != INVALID_FORM && *forms != DEFERRED_FORM ) {
              if ( grabform ) {
                HQASSERT(formclass->num_forms == 0,
                         "More than zero forms in an empty chain");
                formclass->form_chain = *forms ;
                ((HTFORM *)(*forms))->nextf = NULL ;
                formclass->num_forms = 1 ;
                grabform = FALSE ;
              } else {
                free_ht_form( *forms );
              }
            }
            *forms = resetform ;
          }
        }
        if ( resetform ) {
          if ( formclass->form_chain != NULL && formclass->erasenr <= erasenr ) {
            FORM *t1 = formclass->form_chain ;
            FORM *t2 ;

            t2 = (FORM *)(((HTFORM *)t1)->nextf) ;
            (((HTFORM *)t1)->nextf) = NULL ;
            formclass->num_forms = 1 ;

            t1 = t2 ;
            while ( t1 ) {
              t2 = (FORM *)(((HTFORM *)t1)->nextf) ;
              free_ht_form( t1 );
              t1 = t2 ;
            }
          }
        }

        HT_LOCK_RELEASE();

        theINumberCached( levelsrec ) = 0 ;
        if ( ! resetform ) {
          levelsrec->levels_used = 0; levelsrec->erasenr = INVALID_DL;
#ifdef LEVEL_TRACKING
          HqMemZero(levelsrec->level_used, (chptr->notones+1) * sizeof(Bool));
#endif
        }
      } else
        if ( !fReset )
          levelsrec->erasenr = INVALID_DL;
    }
  }
  ht_iterChentryEnd(&iter);
}


/* ---------------------------------------------------------------------- */
#if defined( ASSERT_BUILD )


void debug_display_ht_table(dl_erase_nr dl)
{
  LISTCHALFTONE *listch;
  CHALFTONE *chptr;
  ht_cacheIterator iter;
  SPOTNO c_spotno;
  COLORANTINDEX c_ci;
  HTTYPE c_type;

  if ( !debug_halftonecache )
    return ;

  HQTRACE(debug_halftonecache,("hcol|spo|era| front             . back"));
  ht_iterChentryBegin(&iter, dl, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listch, &chptr, NULL,
                             &c_spotno, &c_type, &c_ci) ) {
    LEVELSRECORD *frontend = chptr != NULL ? theIUsageLevels(chptr) : NULL;
    LEVELSRECORD *backend = chptr != NULL ? theIFormLevels(chptr) : NULL;
    dl_erase_nr eraseno = ht_listch_last_used_dl(listch);

    HQTRACE(debug_halftonecache,
            ("%3d%c %3d %3d | %#8X %2d/%2d %c= %#8X %2d/%2d",
             c_ci, "PTVLD"[c_type], c_spotno, eraseno > 999 ? 999 : eraseno,
             /* Prints 0s for modular, fix if needed */
             frontend, frontend ? theINumberCached(frontend) : 0,
                       frontend ? theILevelsUsed(frontend)   : 0,
             frontend == backend ? '=' : '!',
             backend,  backend  ? theINumberCached(backend)  : 0,
                       backend  ? theILevelsUsed(backend)    : 0));
  }
  ht_iterChentryEnd(&iter);
  HQTRACE(debug_halftonecache,("End of listing halftones"));
}


static void report_ch_caching(dl_erase_nr erasenr)
{
  CHALFTONE *chptr ;
  FORMCLASS *formclass ;

  HQASSERT(erasenr == output_dl, "report_ch_caching called on non-output");

  multi_mutex_lock(&formclasses_mutex);
  for ( formclass = formclasses ;
       formclass ;
       formclass = formclass->next_formclass ) {
    if ( formclass->mru_chptr ) {
      chptr = formclass->mru_chptr ;
      if ( formclass->levels_reqd ) {
#ifdef ASSERT_BUILD
        size_t total_forms = formclass->num_forms + formclass->levels_cached;
        HQTRACE(debug_halftonecache,
                ("** Screens of size: %u, "
                 "Uninitialised forms: %u, Total forms reqd: %d\n"
                 "Percentage allocated: %f, Total bytes: %u",
                 formclass->formsize,
                 formclass->num_forms, formclass->levels_reqd,
                 100.0 * total_forms / formclass->levels_reqd,
                 formclass->formsize * total_forms));
#endif
        while ( chptr ) {
          HQTRACE(debug_halftonecache,
                  ("\tFreq: %f; Angle: %f; Levels used = %d" ,
                   chptr->freqv , chptr->anglv ,
                   chptr->forms != NULL ? chptr->forms->levels_used : 0));
          chptr = chptr->next_chptr ;
        }
      }
    }
  }
  multi_mutex_unlock(&formclasses_mutex);
  debug_display_ht_table(erasenr);
}

#endif /* ASSERT_BUILD */


Bool ht_introduce_dl(dl_erase_nr erasenr, Bool preload)
{
  HQASSERT(erasenr >= oldest_dl, "Introducing DL already dead");
  HQASSERT(erasenr > input_dl, "Introducing DL already seen");
  UNUSED_PARAM(Bool, preload);
  input_dl = erasenr;
  if ( output_dl != INVALID_DL ) /* pipelining */
    ht_form_keep = FALSE;
  return TRUE;
}


void ht_handoff_dl(dl_erase_nr erasenr, Bool separations)
{
  HQASSERT(erasenr >= oldest_dl, "Handing off a DL already dead");
  /* This may be called twice on the same DL for copypage. */
  HQASSERT(erasenr == input_dl || input_dl == INVALID_DL,
           "Handing off an old DL");
  HQASSERT(!separations, "Halftone forms get confused with separations");
  UNUSED_PARAM(dl_erase_nr, erasenr);
  UNUSED_PARAM(Bool, separations)
  input_dl = INVALID_DL;
}


void ht_retire_dl(dl_erase_nr erasenr)
{
  /* This can be called multiple times on the same DL, if a setpagedevice
     fails. Also, by that time, the DL has been flushed as well. */
  if ( erasenr >= oldest_dl )
    unloadchforms(erasenr, FALSE);
  if ( erasenr == input_dl )
    input_dl = INVALID_DL;
  if ( erasenr == output_dl )
    output_dl = INVALID_DL;
}


void ht_flush_dl(dl_erase_nr erasenr)
{
  /* Apparently, this can be called multiple times on the same DL, if a
     setpagedevice fails. */
  if ( oldest_dl <= erasenr )
    oldest_dl = erasenr+1;
#ifdef ASSERT_BUILD
  {
    ht_cacheIterator iter;
    CHALFTONE *chptr;

    ht_iterChentryBegin(&iter, erasenr, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
    while ( ht_iterChentryNext(&iter, NULL, &chptr, NULL, NULL, NULL, NULL) ) {
      LEVELSRECORD *levelsrec;

      if ( chptr == NULL ) /* modular */
        continue;
      levelsrec = chptr->levels;
      do {
        HQASSERT(levelsrec->erasenr == INVALID_DL
                 || levelsrec->erasenr >= oldest_dl,
                 "Levels record escaped retirement!");
        levelsrec = levelsrec->next;
      } while ( levelsrec != chptr->levels );
    }
    ht_iterChentryEnd(&iter);
  }
#endif
}


Bool ht_start_sheet(corecontext_t *context,
                    dl_erase_nr erasenr, GUCR_CHANNEL* hf)
{
  HQASSERT(erasenr >= oldest_dl, "Rendering DL already dead.");
  if ( erasenr != output_dl ) {
    output_dl = erasenr;
    /* Between interpretation and rendering, unload all the forms. This
       will free up memory for forms of subsequent seps, that can be used
       during previous seps. */
    unloadchforms(erasenr, TRUE);
  }
  return ht_preloadchforms(context, erasenr, hf);
}


void ht_end_sheet(dl_erase_nr erasenr, GUCR_CHANNEL* hf, Bool report)
{
  HQASSERT(erasenr == output_dl, "ht_end_sheet on the wrong DL");
  if ( report )
    ht_report_screen_usage(erasenr, hf);
  unloadchforms(erasenr, TRUE);
}



/* ---------------------------------------------------------------------- */
/* Copy some halftone parameters from chptr to ht_params to be used
   by the bitblt routines.  Also copy cachedforms and set lockid. */

void rawstorechglobals(ht_params_t *ht_params, CHALFTONE *chptr)
{
  ht_params->cachedforms = theICachedForms(theIFormLevels(chptr));
  ht_params->lockid = chptr->formclass ? (void*)chptr : NULL;

  ht_params->r1 = theIHalfR1( chptr );
  ht_params->r2 = theIHalfR2( chptr );
  ht_params->r3 = theIHalfR3( chptr );
  ht_params->r4 = theIHalfR4( chptr );
  /* R1 and R3 are based off cos(theta), so are positive for screen angles
     between -90 and +90 degress. R2 and R4 are based off sin(theta), so are
     positive for screen angles between 0 and 180 degrees. */
  if ( (ht_params->r1+ht_params->r3 > 0 && ht_params->r2+ht_params->r4 <= 0) ||
       (ht_params->r1+ht_params->r3 < 0 && ht_params->r2+ht_params->r4 >= 0) ) {
    /* Opposing quadrants -90 to 0, or 90 to 180 degrees. This swap ensures
       that the vectors used for convergence have the same order. The vector
       components are stored as positive values. Across the top of the cell
       (low device space Y), we have r4 then r1. Down the left side of the
       cell (low device space X), we have r3 then r2. */
    register int32 tmp;
    tmp = ht_params->r1; ht_params->r1 = ht_params->r4; ht_params->r4 = tmp;
    tmp = ht_params->r2; ht_params->r2 = ht_params->r3; ht_params->r3 = tmp;
  }
  ht_params->r1 = abs( ht_params->r1 );
  ht_params->r2 = abs( ht_params->r2 );
  ht_params->r3 = abs( ht_params->r3 );
  ht_params->r4 = abs( ht_params->r4 );

  ht_params->ys       = theIHalfYs( chptr );
  ht_params->type     = theIHalfType( chptr );
  ht_params->rotate   = theIHalfRotate( chptr );
  ht_params->repeatb  = theIHalfRepeatB( chptr );
  ht_params->repeatbw = (ht_params->repeatb + BLIT_MASK_BYTES) >> BLIT_SHIFT_BYTES ;

  ht_params->xdims    = theIHalfXDims( chptr );
  ht_params->ydims    = theIHalfYDims( chptr );
  ht_params->exdims   = theIHalfEXDims( chptr );
  ht_params->eydims   = theIHalfEYDims( chptr );

  switch ( theIHalfType( chptr )) {
  case SPECIAL :
  case ONELESSWORD :
  case ORTHOGONAL :
    break ;

  default:
    /* We can reduce the height of the cell, because the lower part is
       replicated in the top part. */
    ht_params->ydims -= min(ht_params->r2, ht_params->r3) ;
    break ;
  }
}


static inline void level_from_tint(int32 *base_out, int32 *level_out,
                                   CHALFTONE * chptr, int32 depth_factor,
                                   int32 tint)
{
  int32 level;

  /* Level-limited thresholds need the supercell ratio before xfer lookup. */
  level = (int32)(chptr->supcell_ratio * tint + .5);
  if ( HalfGetMultithreshold(chptr) ) {
    *base_out = 0;
  } else {
    int32 levels_per_base =
      (int32)(chptr->supcell_ratio * chptr->notones / depth_factor);
    *base_out = level / levels_per_base; level = level % levels_per_base;
  }
  HQASSERT(0 <= *base_out && *base_out < depth_factor, "Tint overflow");
  if ( chptr->thxfer != NULL && !HalfGetExtraGrays(chptr) ) {
    HQASSERT( level >= 0 && level <= chptr->maxthxfer,
              "level (scaled index) out of range for xfer array" );
    level = chptr->thxfer[ level ];
  }
  if (level > chptr->supcell_actual / 2)
    level -= chptr->supcell_remainder;
  HQASSERT(0 <= level && level <= chptr->supcell_actual, "Halftone level overflow");
  *level_out = level;
}

/* ---------------------------------------------------------------------- */
static int32 scan_for_form(CHALFTONE *chptr,
                           register FORM **startf ,
                           register int32 start )
{
  register FORM **endf ;
  register FORM **leftf , **rightf ;

  endf = startf + theINoTones( chptr ) ;
  leftf = startf + start - 1 ;
  rightf = leftf + 2 ;

  /* scan in both directions */
  while ( leftf != startf && rightf != endf ) {
    if ( *leftf ) {
      return CAST_PTRDIFFT_TO_INT32(leftf - startf) ;
    } else if ( *rightf ) {
      return CAST_PTRDIFFT_TO_INT32(rightf - startf) ;
    }
    leftf--;
    rightf++;
  }

  /* scan downwards */
  while ( leftf != startf ) {
    if ( *leftf ) {
      return CAST_PTRDIFFT_TO_INT32(leftf - startf) ;
    }
    leftf-- ;
  }

  /* scan upwards */
  while ( rightf != endf ) {
    if ( *rightf ) {
      return CAST_PTRDIFFT_TO_INT32(rightf - startf) ;
    }
    rightf++;
  }
  return 0 ;
}

/* ---------------------------------------------------------------------- */
static size_t find_random_index(LEVELSRECORD *levelsrec, uint16 notones)
{
  size_t random, i;
  FORM **f ;

  random = (size_t)(hqnrand() * (levelsrec->number_cached - 1)) + 1;

  f = levelsrec->levels;
  for ( i = 1 ; i <= notones ; ++ i )
    if ( f[i] != NULL )
      if ( --random == 0 )
        break;
  HQASSERT(i <= notones,  "Too few forms in levels array");
  return i;
}

/* ---------------------------------------------------------------------- */
static void init_form(CHALFTONE * chptr, const ht_params_t *htp,
                      FORM *render_cache, int32 tint)
{
  int32 level ;
  int32 setbits ;
  int32 diff = 0; /* init to suppress warning */
  int bit_depth = 1 << chptr->depth_shift;
  int32 depth_factor = ((int32)1 << bit_depth) - 1;
  int32 dots = chptr->supcell_actual; /* #distinct ht patterns */
  int32 base; /* base whole tone level to start from */

  HQASSERT( !HT_PARAMS_DEGENERATE(htp), "Degenerate screen inited to a form" );
  HQASSERT( chptr->depth_shift == (uint8)ht_bit_depth_shift(CoreContext.page->hr),
            "Initing a form of the wrong depth" );

  level_from_tint(&base, &level, chptr, depth_factor, tint);

  /* decide whether to go up from base, or down from base+1 */
  setbits = dots - level ;
  if ( setbits > level ) {
    diff = - level ;
  } else {
    ++base; diff = setbits ;
  }
  /* Init the even tone. */
  if ( HalfGetMultithreshold(chptr) ? base == 1 : base == depth_factor )
    area0fill( render_cache );
  else if ( base == 0 )
    area1fill( render_cache );
  else {
    uint8 fill = (uint8)(depth_factor - base);
    int bits = bit_depth;
    for ( ; bits < 8 ; fill |= (uint8)(fill << bits), bits += bits );
    HqMemSet8((uint8 *)render_cache->addr, fill, render_cache->size);
  }

#ifdef METRICS_BUILD
  halftone_metrics.form_resets++ ;
  halftone_metrics.form_reset_bytes += render_cache->size ;
  halftone_metrics.generations++ ;
  halftone_metrics.generation_tints += tint ;
  halftone_metrics.generation_levels += abs(diff) ;
#endif

  if ( diff != 0 ) {
    /* Change the pixels. */
    set_cell_bits(render_cache, htp,
                  theIXCoords( chptr ) + level, theIYCoords( chptr ) + level,
                  diff,
                  theIHalfR1(chptr) , theIHalfR2(chptr) ,
                  theIHalfR3(chptr) , theIHalfR4(chptr) ,
                  theIHalfYs(chptr), bit_depth,
                  theISuperCellMultipleSize(chptr),
                  HalfGetDotCentered(chptr), chptr->accurateScreen,
                  theITHXfer( chptr ) != NULL ) ;
    /* Spread tone over whole form */
    bitexpandform( htp, chptr->halfmxdims, chptr->halfmydims, render_cache );
  }
  render_cache->hoff = HT_FORM_INITED;
}

/* ---------------------------------------------------------------------- */
/* Regenerate a form by copying into it nearest_form and then setting or
   clearing the appropriate bits in the form. */

static Bool regenerate_form(CHALFTONE *chptr, const ht_params_t *htp,
                            FORM **forms, register FORM *theform,
                            int32 index)
{
  int32 bit_depth, depth_factor, nearest_index, diff,
    to_base, from_base, to_level, from_level ;
  FORM *nearest_form ;
  Bool force = (chptr->depth_shift == 0 || HalfGetMultithreshold(chptr)) ;

  bit_depth = 1 << chptr->depth_shift;
  depth_factor = ((int32)1 << bit_depth) - 1;
  nearest_index = scan_for_form(chptr, forms, index) ;

  level_from_tint(&to_base, &to_level, chptr, depth_factor, index);
  level_from_tint(&from_base, &from_level, chptr, depth_factor, nearest_index);

  /* Can only regenerate multi-bit screens if thresholds or the bases
     match. */
  if ( !force && to_base != from_base )
    return FALSE ;

  nearest_form = forms[ nearest_index ] ;

  diff = from_level - to_level ;
  if ( diff == 0 ) {
    if ( nearest_form != theform ) {
      copyform( nearest_form , theform ) ;
#ifdef METRICS_BUILD
      halftone_metrics.form_copies++ ;
      halftone_metrics.form_copy_bytes += theform->size ;
#endif
    }
  } else {
    int32 dots = chptr->supcell_actual; /* #distinct ht patterns */
    int32 setbits = dots - to_level;
    int32 absdiff = diff ;

    if ( diff < 0 )
      absdiff = -absdiff ;

    if ( setbits < absdiff ) {
      if ( !force )
        return FALSE ;
      /* probably cheaper to clear the form and set all the appropriate bits */
      area0fill( theform ) ;
      diff = setbits ;
#ifdef METRICS_BUILD
      halftone_metrics.form_resets++ ;
      halftone_metrics.form_reset_bytes += theform->size ;
#endif
    } else if ( to_level < absdiff ) {
      if ( !force )
        return FALSE ;
      /* probably cheaper to set the form and clear all the appropriate bits */
      area1fill( theform ) ;
      diff = - to_level ;
#ifdef METRICS_BUILD
      halftone_metrics.form_resets++ ;
      halftone_metrics.form_reset_bytes += theform->size ;
#endif
    } else {
      if ( nearest_form != theform ) {
        copyform( nearest_form , theform ) ;
#ifdef METRICS_BUILD
        halftone_metrics.form_copies++ ;
        halftone_metrics.form_copy_bytes += theform->size ;
#endif
      }
    }

    set_cell_bits(theform, htp,
                  theIXCoords(chptr) + to_level, theIYCoords(chptr) + to_level,
                  diff,
                  theIHalfR1(chptr) , theIHalfR2(chptr) ,
                  theIHalfR3(chptr) , theIHalfR4(chptr) ,
                  theIHalfYs(chptr), bit_depth,
                  theISuperCellMultipleSize(chptr),
                  HalfGetDotCentered(chptr), chptr->accurateScreen,
                  theITHXfer( chptr ) != NULL ) ;
    bitexpandform( htp, chptr->halfmxdims, chptr->halfmydims, theform );
  }

#ifdef METRICS_BUILD
  halftone_metrics.regenerations++ ;
  halftone_metrics.regeneration_tints += abs(nearest_index - index) ;
  halftone_metrics.regeneration_levels += abs(diff) ;
#endif

  theform->hoff = HT_FORM_INITED;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
static FORM *poach_form(CHALFTONE *chptr ,
                        FORMCLASS *formclass )
{
  CHALFTONE *pchptr ;
  LEVELSRECORD *plevelsrec ;
  FORM **pforms , *theform ;
  size_t i;

  HT_LOCK_RELEASE() ;
  HT_LOCK_RD_CLAIM( formclass ) ;

  pchptr = formclass->lru_chptr ;

  while ( pchptr ) {

    HT_LOCK_RELEASE() ;
    HT_LOCK_WR_CLAIM( pchptr ) ;

    plevelsrec = theIFormLevels(pchptr) ;
    if ( theINumberCached( plevelsrec ) > 0 ) {
      pforms = theICachedForms( plevelsrec );
      i = find_random_index( plevelsrec, pchptr->notones );
      theform = pforms[ i ] ;
      pforms[ i ] = NULL ;
      theINumberCached( plevelsrec )-- ;
      update_form( theform, theIHalfMXDims(chptr), theIHalfMYDims(chptr)) ;
      HQTRACE(debug_halftonecache,("Poached form %x from %f for %f (%d)",theform,pchptr->anglv,chptr->anglv,i));
      HT_LOCK_RELEASE() ;
      HT_LOCK_WR_CLAIM( chptr ) ;
      return theform ;
    }
    HT_LOCK_RELEASE() ;
    HT_LOCK_RD_CLAIM( formclass ) ;

    pchptr = pchptr->prev_chptr ;
  }

  /* The upper level will re-try so this is not an error */
  return NULL ;
}

/* ---------------------------------------------------------------------- */
/* Sets up a cached halftone from the nearest one available in
   the forms array. */

void getnearest( int32 index, const ht_params_t *ht_params )
{
  LISTCHALFTONE *listchptr ;
  CHALFTONE *chptr ;
  register FORM *theform ;
  register FORM **forms = ht_params->cachedforms ;
  FORMCLASS *formclass ;
  LEVELSRECORD *levelsrec ;

  listchptr = ht_params->listchptr;
  HQASSERT( listchptr , "getnearest called with NULL listchptr" );
  HQASSERT( !ht_listchIsModular(listchptr), "getnearest called on modular" );
  chptr = (CHALFTONE *)ht_params->lockid; /* bit of a hack */
  HQASSERT( chptr, "getnearest called with NULL chptr" );
  levelsrec = theIFormLevels( chptr ) ;
  HQASSERT( preload_failed /* N.B. assert levels before releasing lock */
            || levelsrec->levels_used > levelsrec->number_cached,
            "Ran out of forms" );
#ifdef LEVEL_TRACKING
  HQASSERT( levelsrec->level_used[index], "Unused halftone level used");
#endif

  HT_LOCK_RELEASE();

  formclass = chptr->formclass ;
  for (;;) {

    HT_LOCK_RD_CLAIM( formclass ) ;

    if ( formclass->form_chain ) {

      /* try to grab a form from the formclass chain of forms */

      HT_LOCK_RELEASE() ;
      HT_LOCK_WR_CLAIM( formclass ) ;

      if (( theform = (FORM *)formclass->form_chain ) == NULL ) {
        /* other renderers removed them before we could get one */
        HT_LOCK_RELEASE() ;
        continue ;
      }

      formclass->form_chain = (FORM *)(((HTFORM *)theform)->nextf) ;
      formclass->num_forms-- ;

      update_form( theform, theIHalfMXDims( chptr ), theIHalfMYDims( chptr )) ;

      HQTRACE(debug_halftonecache,("claim wr chptr 1; index: %d, chptr: %x",index,chptr));
      HT_LOCK_RELEASE() ;
      HT_LOCK_WR_CLAIM( chptr ) ;

    } else if ( chptr == formclass->lru_chptr ) {
      /* try to use a form from this screen */

      HT_LOCK_RELEASE() ;
      HT_LOCK_WR_CLAIM( chptr ) ;

      if ( theINumberCached( levelsrec ) == 0 ) {

        theform = poach_form( chptr , formclass ) ;
        if ( ! theform ) {
          HT_LOCK_RELEASE() ;
          continue;
        }
#ifdef METRICS_BUILD
        halftone_metrics.poached++ ;
#endif
      } else {
        /* use a form from this screen */
        size_t form_index = find_random_index( levelsrec, chptr->notones );
        theform = forms[ form_index ] ;
        forms[ form_index ] = NULL ;
        theINumberCached( levelsrec )-- ;
      }
    } else {

      /* poach from the least recently used screen */
      theform = poach_form( chptr , formclass ) ;

      if ( ! theform ) {
        HT_LOCK_RELEASE() ;
        continue;
      }
#ifdef METRICS_BUILD
      halftone_metrics.poached++ ;
#endif
    }

    /* we now have a write lock on the chptr, and theform points to a form
     * which has been removed from the cache.
     */

    if ( forms[ index ] ) {
      /* another renderer has done the work for us, return the form to
       * the formclass chain
       */

      HT_LOCK_RELEASE() ;
      HT_LOCK_WR_CLAIM( formclass ) ;
      ((HTFORM *)theform)->nextf = (HTFORM *)formclass->form_chain ;
      formclass->form_chain = theform ;
      formclass->num_forms++ ;

      HT_LOCK_RELEASE() ;
      HT_LOCK_RD_CLAIM( chptr ) ;

      if ( forms[ index ] ) {
        /* Cast away the constness, because we really do want to modify it. */
        ((ht_params_t*)ht_params)->form = forms[ index ];
        return ;
      }
      else {
        HT_LOCK_RELEASE() ;
        continue ;
      }
    }
    else {
      if ( theINumberCached( levelsrec ) <= 0 ||
           /* Can't regenerate some multibit forms */
           !regenerate_form(chptr, ht_params, forms, theform, index) ) {
        init_form( chptr, ht_params, theform, index );
      }

      forms[ index ] = theform ;
      theINumberCached( levelsrec )++ ;

      /* Move the halftone to the top (MRU) of the chptr linked list */
      if ( formclass->mru_chptr != chptr ) {
        HT_LOCK_RELEASE() ;
        HT_LOCK_WR_CLAIM( formclass ) ;
        if ( formclass->mru_chptr != chptr ) {
          HQASSERT( chptr->prev_chptr , "should be a previous link" ) ;
          chptr->prev_chptr->next_chptr = chptr->next_chptr ;
          if ( chptr->next_chptr )
            chptr->next_chptr->prev_chptr = chptr->prev_chptr ;
          else
            formclass->lru_chptr = chptr->prev_chptr ;
          chptr->next_chptr = formclass->mru_chptr ;
          HQASSERT( chptr->next_chptr , "should be a previous link" ) ;
          chptr->next_chptr->prev_chptr = chptr ;
          chptr->prev_chptr = NULL ;
          formclass->mru_chptr = chptr ;
        }
        HT_LOCK_RELEASE() ;
        HT_LOCK_RD_CLAIM( chptr ) ;
        if ( forms[ index ] ) {
          /* Cast away the constness, because we really do want to modify it. */
          ((ht_params_t*)ht_params)->form = forms[ index ];
          return ;
        }
        else {
          HT_LOCK_RELEASE() ;
          continue ;
        }
      }
      else {
        /* this is the MRU screen, so return once we changed to a read claim */
        HT_LOCK_WR_TO_RD( chptr ) ;
        /* Cast away the constness, because we really do want to modify it. */
        ((ht_params_t*)ht_params)->form = forms[ index ];
        return ;
      }
    }
  }
}


/* ---------------------------------------------------------------------- */
static Bool doscreenforall(DL_STATE *page, OBJECT *report, OBJECT *duplicate,
                           OBJECT *output, OBJECT *proc)
{
  ht_cacheIterator iter;
  LISTCHALFTONE *listchptr;
  CHALFTONE *chptr;
  MODHTONE_REF *mhtref;
  SPOTNO spotno;
  COLORANTINDEX ci;
  HTTYPE type;
  Bool res = FALSE;

  HQASSERT( report    , "report    NULL in doscreenforall" ) ;
  HQASSERT( duplicate , "duplicate NULL in doscreenforall" ) ;
  HQASSERT( output    , "output    NULL in doscreenforall" ) ;
  HQASSERT( proc      , "proc      NULL in doscreenforall" ) ;
  HQASSERT( page->eraseno == input_dl, "doscreenforall not on inputpage" );

  if ( !oBool( *duplicate )
       && (oName( *report ) == system_names + NAME_All
           || oName( *report ) == system_names + NAME_Current) ) {
    ht_iterChentryBegin(&iter, page->eraseno,
                        SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
    while ( ht_iterChentryNext(&iter, NULL, &chptr, &mhtref,
                               &spotno, NULL, NULL) ) {
      if ( oName( *report ) == system_names + NAME_All ||
           spotno == gsc_getSpotno(gstateptr->colorInfo)) {
        Bool report_this =
          oName( *report ) == system_names + NAME_Current
          || ( chptr != NULL ? chptr->usage->erasenr == input_dl
               : htm_is_used(mhtref, input_dl) );
        if ( chptr != NULL )
          chptr->reported = (int8)!report_this;
        else
          mhtref->reported = !report_this;
      }
    }
    ht_iterChentryEnd(&iter);
  }

#define return DO_NOT_return_SET_res_AND_goto_fail_INSTEAD!
  ht_iterChentryBegin(&iter, page->eraseno,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listchptr, &chptr, &mhtref,
                             &spotno, &type, &ci) ) {
    if ( ci != COLORANTINDEX_ALL && ci != COLORANTINDEX_NONE ) {
      /* Do we need to consider this screen? */
      if (( oName( *report ) == system_names + NAME_All ||
            oName( *report ) == system_names + NAME_New ||
            ( oName( *report ) == system_names + NAME_Current &&
              spotno == gsc_getSpotno( gstateptr->colorInfo ))) &&
          ( oBool( *duplicate )
            || (chptr != NULL ? chptr->reported : mhtref->reported) )) {
        int32 j ;
        OBJECT *element ;
        Bool anexit = TRUE;
        Bool isSpotFuncScreen = mhtref == NULL &&
                                ( ! theITHXfer( chptr ) ) ;
        /* Sequence through the array pushing all requested values. */
        for ( j = 0 , element = oArray( *output ) ;
              j < theLen(*output) ;
              j++ , element++ ) {
          if ( oType( *element ) != ONAME ) {
            res = error_handler( TYPECHECK );
            goto fail;
          }
          switch ( oNameNumber( *element )) {
          case NAME_Frequency:
            if ( ! stack_push_real( !isSpotFuncScreen ? 60.0 : chptr->freqv,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_Angle:
            if ( ! stack_push_real( !isSpotFuncScreen ? 0.0 : chptr->anglv,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_SpotFunction:
            if ( !isSpotFuncScreen ) {
              if ( ! push(&onull, &operandstack) )
                goto fail;
            }
            else {
              OBJECT *spotfunction = NULL;

              /* Go find spot function; maybe from override/switchscreen. */
              if ( theISFName( chptr )) {
                OBJECT *overrideo ;
                overrideo = findSpotFunctionObject(theISFName(chptr)) ;
                if ( overrideo ) {
                  if ( oType( *overrideo ) == OARRAY ||
                       oType( *overrideo ) == OPACKEDARRAY ) {
                    spotfunction = overrideo ;
                  }
                  else if ( oType( *overrideo ) == ODICTIONARY ) {
                    if ( ht_listchSFColor(listchptr) )
                      spotfunction =
                        ht_extract_spotfunction( overrideo,
                                                 ht_listchSFColor(listchptr) );
                  }
                }
              }
              if ( spotfunction == NULL )
                spotfunction =
                  gs_extract_spotfunction( spotno, ci,
                                           ht_listchSFColor(listchptr) );
              /* So did we find it anywhere... If not then push a NULL. */
              if ( ! push( spotfunction != NULL ?
                           spotfunction : &onull, &operandstack) )
                goto fail;
            }
            break ;
          case NAME_AccurateScreens:
            if ( ! push( isSpotFuncScreen && theIAccurateScreen( chptr ) ?
                         & tnewobj : & fnewobj,
                         & operandstack ))
              goto fail;
            break ;
          case NAME_PatternScreen:
            if ( ! push( chptr != NULL && chptr->formclass == NULL ?
                         & tnewobj : & fnewobj ,  & operandstack ))
              goto fail;
            break ;
          case NAME_ActualFrequency:
            if ( ! stack_push_real( !isSpotFuncScreen ? 60.0 : chptr->ofreq,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_ActualAngle:
            if ( ! stack_push_real( !isSpotFuncScreen ? 0.0 : chptr->oangle,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_FrequencyDeviation:
            if ( ! stack_push_real( !isSpotFuncScreen ? 60.0 : chptr->dfreq,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_FrequencyAccuracy:
            if ( ! stack_push_real( !isSpotFuncScreen ? 0.0 : chptr->efreq,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_AngleAccuracy:
            if ( ! stack_push_real( !isSpotFuncScreen ? 0.0 : chptr->eangle,
                                    &operandstack ))
              goto fail;
            break ;
          case NAME_HalftoneType:
            if ( ! stack_push_integer( mhtref != NULL ? 100
                                       : isSpotFuncScreen ?  1 : 3,
                                       &operandstack ))
              goto fail;
            break ;
          case NAME_HalftoneName: {
            NAMECACHE *htname = ht_listchHalfToneName(listchptr);

            oName( nnewobj ) =
              chptr != NULL && chptr->sfname ? chptr->sfname
              : (htname != NULL ? htname : system_names + NAME_Unknown);
            if ( ! push( & nnewobj , & operandstack ))
              goto fail;
          } break ;
          case NAME_HalftoneModule:
            if ( !ht_listchIsModular(listchptr) ) {
              if ( !push(&onull, &operandstack) )
                goto fail;
            } else {
              const uint8 *name = ht_listchModularName(listchptr);
              OBJECT ostring = OBJECT_NOTVM_NOTHING ;
              if ( !ps_string(&ostring, name, strlen_int32((char*)name)) ||
                   !push(&ostring, &operandstack) )
                goto fail;
            }
            break;
          case NAME_HalftoneColor:
            HQASSERT(ht_listchSFColor( listchptr ) != NULL,
                     "no color name set for screen");
            oName( nnewobj ) = ht_listchSFColor( listchptr );
            if ( ! push( & nnewobj , & operandstack ))
              goto fail;
            break ;
          case NAME_ScreenIndex: {
            int32 si;
            if ( DOING_RUNLENGTH(page) &&
                 gucr_interleavingStyle(page->hr) == GUCR_INTERLEAVINGSTYLE_PIXEL )
              si = spotno;
            else if ( chptr == NULL || chptr->formclass == NULL )
              si = -1;
            else
              si = theIScreenIndex (chptr);
            if ( ! stack_push_integer( si, &operandstack ) )
              goto fail;
          } break ;
          case NAME_ColorIndex:
            if ( ! stack_push_integer( ci, &operandstack))
              goto fail;
            break ;
          case NAME_ObjectType:
            if ( ! stack_push_integer( type, &operandstack ))
              goto fail;
            break ;
          default:
            res = error_handler( TYPECHECK );
            goto fail;
          }
        } /* for args array */
        /* Now call the recursive interpreter given all the values pushed. */
        if ( ! push( proc , & executionstack ))
          goto fail;

        if ( ! interpreter( 1 , & anexit )) {
          if ( ! anexit )
            goto fail;
          else {
            error_clear();
            break;
          }
        }

        if ( chptr != NULL )
          chptr->reported = TRUE;
        else
          mhtref->reported = TRUE;
      }
    }
  }
  res = TRUE;
 fail:
  ht_iterChentryEnd(&iter);
#undef return
  return res;
}

/* ---------------------------------------------------------------------- */
Bool screenforall_(ps_context_t *pscontext)
{
  Bool result ;
  OBJECT *theo ;
  int32 namecnt = 0 ;
  OBJECT names[ 8 ] = {
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING,
    OBJECT_NOTVM_NOTHING, OBJECT_NOTVM_NOTHING
  } ;

  enum {
    sfa_Report, sfa_Duplicate, sfa_Output, sfa_n_entries
  } ;
  static NAMETYPEMATCH screenforall_match[sfa_n_entries + 1] = {
    { NAME_Report, 1, { ONAME }},
    { NAME_Duplicate, 1, { OBOOLEAN }},
    { NAME_Output, 2, { OARRAY, OPACKEDARRAY }},
    DUMMY_END_MATCH
  };

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;
  /* get the boolean (or integer) and the proc off the stack */
  theo = theTop( operandstack ) ;

  if ( oType( *theo ) == ODICTIONARY ) {
      /* Final completely flexible style (set of possibilites, ? at end means optional):
       * {...} <<
       *   /Report "New/All/Current"
       *   /Duplicate "true/false"
       *   /Output [ /Frequency? /Angle? /SpotFunction?
       *             /AccurateScreens?
       *             /PatternScreen?
       *             /ActualFrequency? /ActualAngle?
       *             /FrequencyDeviation?
       *             /FrequencyAccuracy? /AngleAccuracy?
       *             /HalftoneType? /HalftoneName? /HalftoneColor?
       *             /ScreenIndex?
       *             /ColorIndex? ]
       * >> screenforall
       */

    int32 i ;
    int32 len ;
    OBJECT *olist ;
    if ( ! oCanRead( *oDict( *theo )) &&
         ! object_access_override(oDict(*theo)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( theo , screenforall_match ))
      return FALSE ;
    len = theLen(*screenforall_match[sfa_Output].result) ;
    olist = oArray(*screenforall_match[sfa_Output].result) ;
    for ( i = 0 ; i < len ; ++i )
      if ( oType( olist[ i ] ) != ONAME )
        return error_handler( TYPECHECK ) ;
  }
  else {
    Bool push_color ;
    Bool notsupplied ;
    if ( oType( *theo ) == OBOOLEAN ) {
      /* Original style (set of possibilites, ? at end means optional):
       * <<
       *   /Report "New/All"
       *   /Duplicate false
       *   /Output [ /Frequency /Angle /SpotFunction /ScreenIndex ]
       * >> {...} screenforall
       */

      push_color = FALSE ;
      notsupplied = oBool( *theo ) ;
    } else if ( oType( *theo ) == OINTEGER ) {
      /* Subsequent to original style (set of possibilites, ? at end means optional):
       * <<
       *   /Report "New/All"
       *   /Duplicate false
       *   /Output [ /Frequency /Angle /SpotFunction /ScreenIndex /ColorIndex? ]
       * >> {...} screenforall
       */

      int32 arg ;
      arg = oInteger( *theo ) ;
      if ( arg < 0 || arg > 3 )
        return error_handler( RANGECHECK ) ;
      push_color  = ((arg & 2) != 0) ;
      notsupplied = ((arg & 1) != 0) ;
    }
    else
      return error_handler( TYPECHECK ) ;

    /* Fill in screenforall_match... */
    object_store_name(&names[namecnt], (notsupplied ? NAME_New : NAME_All), LITERAL) ;
    screenforall_match[sfa_Report].result = ( & names[ namecnt ] ) ;
    namecnt++ ;

    OCopy(names[namecnt], fnewobj) ;
    screenforall_match[sfa_Duplicate].result = ( & names[ namecnt ] ) ;
    namecnt++ ;

    theTags( names[ namecnt ] ) = OARRAY ;
    theLen( names[ namecnt ] ) = ( uint16 )( 4 + ( push_color ? 1 : 0 )) ;
    oArray( names[ namecnt ] ) = ( & names[ namecnt + 1 ] ) ;
    screenforall_match[sfa_Output].result = ( & names[ namecnt ] ) ;
    namecnt++ ;

    object_store_name(&names[namecnt], NAME_Frequency, LITERAL) ;
    namecnt++ ;

    object_store_name(&names[namecnt], NAME_Angle, LITERAL) ;
    namecnt++ ;

    object_store_name(&names[namecnt], NAME_SpotFunction, LITERAL) ;
    namecnt++ ;

    object_store_name(&names[namecnt], NAME_ScreenIndex, LITERAL) ;
    namecnt++ ;

    object_store_name(&names[namecnt], NAME_ColorIndex, LITERAL) ;
    namecnt++ ;

    HQASSERT( namecnt == NUM_ARRAY_ITEMS(names) , "array wrong size" ) ;
  }

  theo = stackindex( 1, & operandstack ) ;

  switch ( oType( *theo )) {
  case OARRAY:
  case OPACKEDARRAY:
    break;
  default:
    return error_handler( TYPECHECK );
  }
  if ( ! oCanExec( *theo ))
    return error_handler( INVALIDACCESS ) ;

  if ( ! push( theo , & temporarystack ))
    return FALSE ;
  theo = theTop( temporarystack ) ;
  npop( 2 , & operandstack ) ;

  result = doscreenforall( ps_core_context(pscontext)->page ,
                           screenforall_match[sfa_Report].result ,
                           screenforall_match[sfa_Duplicate].result ,
                           screenforall_match[sfa_Output].result ,
                           theo ) ;
  pop( & temporarystack ) ;
  return result ;
}

/* ---------------------------------------------------------------------- */

void mknamestr(uint8 *buff, NAMECACHE *name)
{
  static uint8 * unknown = UVS( "Unknown" );

  if (name && theINLen(name) < LONGESTSCREENNAME) {
    HqMemCpy(buff, theICList(name), (int32)theINLen(name));
    buff[theINLen(name)] = '\0' ;
  }
  else {
    HqMemCpy(buff, unknown, strlen_int32((char*)unknown) + 1);
  }
}

/* ---------------------------------------------------------------------- */

#define SET_SCREEN_FLOAT(dev, name, value) { \
  DEVICEPARAM p; \
  int32 res ; \
  theDevParamName(p)    = (uint8 *)(name); \
  theDevParamNameLen(p) = (int32)sizeof("" name "") - 1; \
  theDevParamType(p)    = ParamFloat; \
  theDevParamFloat(p)   = (float)(value); \
  res = (*theISetParam(dev))(dev, &p) ; \
  if ( res != ParamAccepted && res != ParamIgnored ) \
    break; \
}

#define SET_SCREEN_INT(dev, name, value) { \
  DEVICEPARAM p; \
  int32 res ; \
  theDevParamName(p)    = (uint8 *)(name); \
  theDevParamNameLen(p) = (int32)sizeof("" name "") - 1; \
  theDevParamType(p)    = ParamInteger; \
  theDevParamInteger(p) = value; \
  res = (*theISetParam(dev))(dev, &p) ; \
  if ( res != ParamAccepted && res != ParamIgnored ) \
    break; \
}

#define SET_SCREEN_BOOL(dev, name, value) { \
  DEVICEPARAM p; \
  int32 res ; \
  theDevParamName(p)    = (uint8 *)(name); \
  theDevParamNameLen(p) = (int32)sizeof("" name "") - 1; \
  theDevParamType(p)    = ParamBoolean; \
  theDevParamInteger(p) = value; \
  res = (*theISetParam(dev))(dev, &p) ; \
  if ( res != ParamAccepted && res != ParamIgnored ) \
    break; \
}

#define SET_SCREEN_STRING(dev, name, value, length) { \
  DEVICEPARAM p; \
  int32 res ; \
  theDevParamName(p)    = (uint8 *)(name); \
  theDevParamNameLen(p) = (int32)sizeof("" name "") - 1; \
  theDevParamType(p)    = ParamString; \
  theDevParamString(p)  = value; \
  theDevParamStringLen(p) = length; \
  res = (*theISetParam(dev))(dev, &p) ; \
  if ( res != ParamAccepted && res != ParamIgnored ) \
    break; \
}


void ht_delete_export_screens( void )
{
  if ( screendevice != NULL )
    (void)(*theIDeleteFile( screendevice ))( screendevice, NULL ) ;

  return;
}


static Bool ht_export_chptr(
  CHALFTONE*        chptr,
  LISTCHALFTONE    *listchptr,
  DEVICELIST*       screendev,
  int32             screen_index,
  uint32            rle_colorant,
  int32             defaultScreenIndex,
  Bool              use_screen_cache_name,
  Bool              screen_dot_centered)
{
  DEVICE_FILEDESCRIPTOR desc;
  uint8 screenname[LONGESTFILENAME];
  int32 phasex, phasey ;

  ht_listchPhaseXY(listchptr, &phasex, &phasey) ;

  phasex = theIHalfXDims(chptr) - phasex;
  phasey = theIHalfYDims(chptr) - phasey;

  /* MRW - this is in a do ... while (FALSE) as all the macros can do a
     break to prevent any subsequent lines doing anything - yuck -
     which can be tested for by desc changin from -1 to 0 */
  /* write out the freq, angle, and spot function parameters */
  do {
    desc = -1 ;
    SET_SCREEN_FLOAT(screendev, "ScreenFrequency", chptr->freqv) ;
    SET_SCREEN_FLOAT(screendev, "ScreenAngle", chptr->anglv) ;
    SET_SCREEN_INT(screendev, "ScreenIndex", screen_index);

    if (use_screen_cache_name) {
      SET_SCREEN_STRING(screendev, "ScreenSpotName",
                        chptr->path, (int32)strlen((char *)chptr->path));
    } else {
      mknamestr(screenname, chptr->sfname);
      SET_SCREEN_STRING(screendev, "ScreenSpotName",
                        screenname, (int32)strlen((char *)screenname));
    }

    SET_SCREEN_BOOL(screendev, "ScreenDefault",
                    defaultScreenIndex == screen_index);
    SET_SCREEN_INT(screendev, "ScreenId", chptr->screen_index);
    SET_SCREEN_INT(screendev, "ScreenColorant", (int32)rle_colorant);

    SET_SCREEN_INT(screendev, "ScreenR1", chptr->halfr1);
    SET_SCREEN_INT(screendev, "ScreenR2", chptr->halfr2);
    SET_SCREEN_INT(screendev, "ScreenR3", chptr->halfr3);
    SET_SCREEN_INT(screendev, "ScreenR4", chptr->halfr4);
    SET_SCREEN_INT(screendev, "ScreenPhaseX", phasex);
    SET_SCREEN_INT(screendev, "ScreenPhaseY", phasey);

    desc = 0 ;
  } while ( FALSE ) ;

  if ( desc >= 0 ) {
    desc = (*theIOpenFile( screendev ))( screendev , NULL, SW_WRONLY ) ;
  }

  /* Write out the screen details if it is a Harlequin screen dev. */
  /** \todo ajcd 2010-05-28: Why is this protected by UseScreenCacheName? */
  if ( !use_screen_cache_name && desc >= 0 ) {
    ExportScreen hqn_screen ;

    hqn_screen.r1 = chptr->halfr1 ;
    hqn_screen.r2 = chptr->halfr2 ;
    hqn_screen.r3 = chptr->halfr3 ;
    hqn_screen.r4 = chptr->halfr4 ;
    hqn_screen.halftype = chptr->halftype ;
    hqn_screen.supcell_ratio = chptr->supcell_ratio ;
    hqn_screen.supcell_multiplesize = chptr->supcell_multiplesize ;
    hqn_screen.supcell_actual = chptr->supcell_actual ;
    hqn_screen.accurateScreen = chptr->accurateScreen ;
    hqn_screen.notones = chptr->notones ;
    if ( ( chptr->screenprotection & SCREENPROT_ENCRYPT_MASK ) == SCREENPROT_ENCRYPT_NONE ) {
      hqn_screen.xs = chptr->xcoords ;
      hqn_screen.ys = chptr->ycoords ;
    } else {
      hqn_screen.xs = NULL ;
      hqn_screen.ys = NULL ;
    }
    hqn_screen.ScreenDotCentered = screen_dot_centered ;
    hqn_screen.supcell_remainder = chptr->supcell_remainder ;

    (void)(*theIWriteFile( screendev ))( screendev, desc,
                                         (uint8 *)&hqn_screen,
                                         sizeof( ExportScreen )) ;
  }

  if ( desc >= 0 ) {
    (void)(*theICloseFile( screendev ))( screendev, desc) ;
  }

  return desc >= 0 ;
}


Bool ht_export_screens(DL_STATE *page, GUCR_RASTERSTYLE* hr, GUCR_CHANNEL* hf,
                       RleColorantMap *map, Bool use_screen_cache_name,
                       Bool screen_dot_centered)
{
  Bool result = TRUE ;
  int32 default_index;
  GUCR_COLORANT *hc;
  COLORANTINDEX ci = COLORANTINDEX_UNKNOWN;
  Bool mono;

#if defined( ASSERT_BUILD )
  report_ch_caching(page->eraseno);
#endif

  if ( screendevice == NULL ) {
    DEVICEPARAM param ;

    screendevice = find_device((uint8*)"screeninfo");
    if ( screendevice == NULL ) {
      return TRUE;
    }

    /* find out if it is a Harlequin screen device */
    theDevParamName(param) = (uint8*)"PageSize";
    theDevParamNameLen(param) = 8;
  }

  /* Find first active colorant in frame */
  for ( hc = gucr_colorantsStart(hf) ;
        gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED) ;
        gucr_colorantsNext(&hc) ) {
    const GUCR_COLORANT_INFO *colorantInfo ;
    if ( gucr_colorantDescription(hc, &colorantInfo) ) {
      ci = colorantInfo->colorantIndex ;
      break ;
    }
  }

  HQASSERT(ci != COLORANTINDEX_UNKNOWN,
           "ht_export_screens: failed to find any known colorants for frame");

  /* The default screen is the one with the default spotno (from page) for the
   * colorant of this frame, or for color RLE, simply the spotno. */
  mono = gucr_interleavingStyle(hr) != GUCR_INTERLEAVINGSTYLE_PIXEL;
  if ( mono )
    default_index = ht_screen_index(page->default_spot_no, HTTYPE_DEFAULT, ci);
  else
    default_index = page->default_spot_no;

  /* Loop over all colorants for this frame starting at first active colorant */
  clear_screen_mark(page->eraseno);
  for ( ; result && gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED) ;
        gucr_colorantsNext(&hc) ) {
    const GUCR_COLORANT_INFO *colorantInfo ;

    if ( gucr_colorantDescription(hc, &colorantInfo) ) {
      ht_cacheIterator iter ;
      LISTCHALFTONE *listchptr ;
      CHALFTONE *chptr ;
      SPOTNO spotno ;
      uint32 rle_colorant; /* default when no real screens on the page */
      (void)rleColorantMapGet(map, colorantInfo->colorantIndex, &rle_colorant);

      /* Iterate all spots for this colorant, including defaults. */
      HQASSERT(page->eraseno == output_dl, "Export outside rendering");
      ht_iterChentryBegin(&iter, page->eraseno,
                          SPOT_NO_INVALID, colorantInfo->colorantIndex);
#define return DO_NOT_return_SET_result_AND_break_INSTEAD!
      while ( ht_iterChentryNext(&iter, &listchptr, &chptr, NULL, &spotno, NULL, NULL) ) {
        Bool newscreen ;

        if ( chptr == NULL ) {
          result = detail_error_handler(CONFIGURATIONERROR,
                                        "Modular halftones not compatible with RLE output") ;
          break ;
        }

        newscreen = !theIScreenMarked(chptr) ;
        if ( !newscreen && mono ) {
          /* In mono RLE, the screen index is used for export rather than
             the spotno, and this is a unique mapping, so we can skip this
             screen. Several spotnos can map to the same CHALFTONE, so if not
             screening, we need to map each spotno to the screen details. */
          continue ;
        }
        theIScreenMarked(chptr) = TRUE;

        /* Report and export only if not a pattern screen and used, since
           pattern screens have been translated to level 2 patterns for
           RLE. */
        if ( chptr->formclass != NULL &&
             chptr->forms != NULL && chptr->forms->erasenr == output_dl ) {
          Bool ok = ht_export_chptr(chptr, listchptr, screendevice,
                                    mono ? theIScreenIndex(chptr) : spotno,
                                    rle_colorant,
                                    newscreen ? default_index : -1,
                                    use_screen_cache_name,
                                    screen_dot_centered) ;
          if ( !ok ) {
            result = device_error_handler(screendevice);
            break ;
          }
        }
      } /* chptr iterator */
#undef return
      ht_iterChentryEnd(&iter);
    }
  } /* foreach separation loop */
  return result;
} /* Function ht_export_screens */

/* ---------------------------------------------------------------------- */

void ht_dot_shape_string(NAMECACHE *htname,
                         NAMECACHE *sfname,
                         Bool intToExt ,
                         uint8 nambuf[ LONGESTSCREENNAME * 2 ] )
{
  uint8 nambuf2[ LONGESTSCREENNAME ] ;

  if ( ! sfname ) {
    static uint8 * unknown = UVS( "Unknown" );
    HqMemCpy( nambuf, unknown, ( int32 )strlen ((char*)unknown) + 1 );
    /* If sfname is null then so should htname, otherwise we could
     * print that instead. */
    HQASSERT( ! htname , "htname not null in ht_dot_shape_string." ) ;
    return ;
  }

  if ( intToExt ) {
    /* convert the internal name to an external name */
    if ( ! ht_getExternalScreenName( nambuf , sfname , LONGESTSCREENNAME ))
      mknamestr( nambuf , sfname ) ;
  }
  else
    mknamestr( nambuf , sfname ) ;

  if ( htname ) {
    /* Adobe have named the Euclidean spot function 'Round' in PDF, and
     * the Round spot function 'SimpleDot', and so on. Should any of
     * these cases occur, the message reported to the monitor window
     * will be changed to avoid confusing the user.  E.g. "Euclidean
     * [Round(PDF)]" instead of "Euclidean".
     */
    if ( ( htname == system_names + NAME_SimpleDot &&
           sfname == system_names + NAME_Round ) ||
         ( htname == system_names + NAME_Round &&
           sfname == system_names + NAME_Euclidean ) ||
         ( htname == system_names + NAME_Line &&
           sfname == system_names + NAME_LineAdobe ) ||
         ( htname == system_names + NAME_Rhomboid &&
           sfname == system_names + NAME_RhomboidAdobe ) ) {
      if ( intToExt ) {
        /* convert the internal name to an external name */
        if ( ! ht_getExternalScreenName( nambuf2 , htname , LONGESTSCREENNAME ))
          mknamestr( nambuf2 , htname ) ;
      }
      else
        mknamestr( nambuf2 , htname ) ;
      swcopyf( nambuf + strlen(( char * )nambuf ) , (uint8 *)" [%s(PDF)]" , nambuf2 ) ;
    }
  }
}


static char* HTTYPE_to_name(HTTYPE type)
{
  switch (type) {
  case REPRO_TYPE_PICTURE: return "Picture"; break;
  case REPRO_TYPE_TEXT: return "Text"; break;
  case REPRO_TYPE_VIGNETTE: return "Vignette"; break;
  case REPRO_TYPE_OTHER: return "Other"; break;
  case HTTYPE_DEFAULT: return "Default"; break;
  default: HQFAIL("Invalid ht type"); return "Unknown";
  }
}


typedef struct {
  CHALFTONE *chptr;
  MODHTONE_REF *mhtref;
  dl_erase_nr erasenr;
  SWMSG_HT_USAGE_COLORANT ht_colorant;
  Bool first_one;
  Bool colorant_reported[REPRO_N_TYPES+1][COLORANTINDEX_MAX+1];
} report_colorant_struct;


/** Issues colorant usage events for this colorant, when it uses the screen in
    \c arg.

  The events are issued lagging by one, when the next colorant/type match is
  found. \c ht_colorant is used to remember the previous one. The last one is
  issued after the end of the iteration, so it can be specially flagged.
 */
static Bool ht_report_colorant(const GUCR_COLORANT_INFO *colInfo,
                               void *arg)
{
  report_colorant_struct *col_ctx = arg;
  COLORANTINDEX sheet_ci = colInfo->colorantIndex;
  ht_cacheIterator iter;
  CHALFTONE *chptr;
  MODHTONE_REF *mhtref;
  COLORANTINDEX ci;
  HTTYPE type;
  SPOTNO spotno;
  char* type_name;

  ht_iterChentryBegin(&iter, col_ctx->erasenr,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, NULL, &chptr, &mhtref,
                             &spotno, &type, &ci) ) {
    if ( chptr != col_ctx->chptr || mhtref != col_ctx->mhtref )
      continue;
    if ( col_ctx->colorant_reported[type][sheet_ci] )
      continue;
    if ( ci != COLORANTINDEX_NONE
         ? ci != sheet_ci
         /* if lookup returns an exact match, Default is not used for this */
         : ht_getlistch_exact(spotno, type, sheet_ci) != NULL )
      continue;
    col_ctx->colorant_reported[type][sheet_ci] = TRUE;
    /* Now found a matching colorant, issue event for previous one, if any. */
    if ( col_ctx->first_one )
      col_ctx->first_one = FALSE;
    else {
      col_ctx->ht_colorant.last_one = FALSE;
      if ( SwEvent(SWEVT_HT_USAGE_COLORANT,
                   &col_ctx->ht_colorant, sizeof(col_ctx->ht_colorant))
           >= SW_EVENT_ERROR )
        HQTRACE(TRUE,
                ("Error from SwEvent(SWEVT_HT_USAGE_COLORANT, 0x%p, %u)",
                 &col_ctx->ht_colorant, sizeof(col_ctx->ht_colorant))) ;
    }
    col_ctx->ht_colorant.colorant_name.length = colInfo->name->len;
    col_ctx->ht_colorant.colorant_name.string = colInfo->name->clist;
    type_name = HTTYPE_to_name(type);
    col_ctx->ht_colorant.type_name.length = strlen(type_name);
    col_ctx->ht_colorant.type_name.string = (uint8*)type_name;
  }
  ht_iterChentryEnd(&iter);
  return TRUE;
}


typedef struct {
  dl_erase_nr erasenr;
  GUCR_CHANNEL* hf;
  corecontext_t *context;
} report_screens_struct;


static Bool ht_report_screens_colorant(const GUCR_COLORANT_INFO *colInfo,
                                       void *arg)
{
  report_screens_struct *sheet_ctx = arg;
  COLORANTINDEX rep_ci = colInfo->colorantIndex;
  sw_tl_ref sheet_tl = sheet_ctx->context->page->timeline;
  ht_cacheIterator iter;
  LISTCHALFTONE* listchptr;
  CHALFTONE *chptr;
  MODHTONE_REF *mhtref;
  COLORANTINDEX ci, i_ci;
  HTTYPE type, i_type;
  SPOTNO spotno;
  static int token = 0;

  /* Loop over all cache entries */
  ht_iterChentryBegin(&iter, sheet_ctx->erasenr,
                      SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, &listchptr, &chptr, &mhtref,
                             &spotno, &type, &ci) ) {
    NAMECACHE* sfcolor = ht_listchSFColor(listchptr);
    char* type_name;
    report_colorant_struct col_ctx;

    if ( ci != COLORANTINDEX_NONE
         ? ci != rep_ci
         /* if lookup returns an exact match, Default is not used for this */
         : ht_getlistch_exact(spotno, type, rep_ci) != NULL )
      continue;

    if ( chptr != NULL /* not modular */ ) {
      uint8 nambuf[LONGESTSCREENNAME*2];
      LEVELSRECORD *rec;

      if ( chptr->screenmark )
        continue;
      chptr->screenmark = TRUE;

      /* Check if it's a real screen, and actually used at all. */
      if ( chptr->notones < 2 || chptr->formclass == NULL )
        continue;
      rec = chalftone_output_levels(chptr);
      if ( rec == NULL || rec->levels_used == 0 ) /* wasn't used */
        continue;

      ht_dot_shape_string(ht_listchHalfToneName(listchptr), theISFName(chptr),
                          TRUE, nambuf);
      if ( chptr->thxfer ) {
        SWMSG_HT_USAGE_THRESHOLD ht_usage ;

        ht_usage.timeline = sheet_tl;
        ht_usage.screen_name.length = strlen((char*)nambuf);
        ht_usage.screen_name.string = nambuf;
        if ( chptr->reportme == SPOT_FUN_RAND /* 1st-gen HDS */ ) {
          char *cols[5] = {"Cyan", "Magenta", "Yellow", "Black", "Default"};
          HQASSERT( chptr->reportcolor >= 0 && chptr->reportcolor < 5,
                    "cols index out of range" ) ;
          ht_usage.colorant_name.length = strlen(cols[chptr->reportcolor]);
          ht_usage.colorant_name.string = (uint8*)cols[chptr->reportcolor];
        } else if ( sfcolor ) {
          ht_usage.colorant_name.length = theINLen(sfcolor);
          ht_usage.colorant_name.string = theICList(sfcolor);
        } else {
          ht_usage.colorant_name.length = 0;
          ht_usage.colorant_name.string = NULL;
        }
        type_name = HTTYPE_to_name(type);
        ht_usage.type_name.length = strlen(type_name);
        ht_usage.type_name.string = (uint8*)type_name;
        ht_usage.tones_used = rec->levels_used + 2;
        ht_usage.tones_total = chptr->notones + 1;
        ht_usage.token = ++token;
        if ( SwEvent(SWEVT_HT_USAGE_THRESHOLD, &ht_usage, sizeof(ht_usage)) >= SW_EVENT_ERROR )
          HQTRACE(TRUE,
                  ("Error from SwEvent(SWEVT_HT_USAGE_THRESHOLD, 0x%p, %u)",
                   &ht_usage, sizeof(ht_usage))) ;
      } else {
        SWMSG_HT_USAGE_SPOT ht_usage ;

        ht_usage.timeline = sheet_tl;
        ht_usage.function_name.length = strlen((char*)nambuf);
        ht_usage.function_name.string = nambuf;
        ht_usage.frequency = chptr->freqv;
        ht_usage.angle = chptr->anglv;
        ht_usage.unoptimized_angle = HalfGetAngleUnoptimized(chptr);
        ht_usage.deviated_frequency = chptr->dfreq;
        ht_usage.frequency_inaccuracy = chptr->efreq;
        ht_usage.angle_inaccuracy = chptr->eangle;
        ht_usage.excessive_inaccuracy = HalfGetOptimizationErrorExceeded(chptr);
        ht_usage.tones_used = rec->levels_used + 2;
        ht_usage.tones_total = chptr->notones + 1;
        ht_usage.token = ++token;
        if ( SwEvent(SWEVT_HT_USAGE_SPOT, &ht_usage, sizeof(ht_usage)) >= SW_EVENT_ERROR )
          HQTRACE(TRUE,
                  ("Error from SwEvent(SWEVT_HT_USAGE_SPOT, 0x%p, %u)",
                   &ht_usage, sizeof(ht_usage))) ;
      }
    } else { /* modular */
      SWMSG_HT_USAGE_MODULAR ht_usage;
      const uint8 *module_name;
      NAMECACHE *screen_name;

      if ( mhtref->screenmark )
        continue;
      mhtref->screenmark = TRUE;

      if ( !htm_is_used(mhtref, output_dl) )
        continue;

      ht_usage.timeline = sheet_tl;
      screen_name = ht_listchHalfToneName(listchptr);
      if ( screen_name != NULL ) {
        ht_usage.screen_name.length = theINLen(screen_name);
        ht_usage.screen_name.string = theICList(screen_name);
      } else {
        ht_usage.screen_name.length = 0;
        ht_usage.screen_name.string = NULL;
      }
      module_name = mhtref->mod_entry->impl->info.display_name;
      ht_usage.module_name.length = strlen((char*)module_name);
      ht_usage.module_name.string = module_name;
      if ( sfcolor ) {
        ht_usage.colorant_name.length = theINLen(sfcolor);
        ht_usage.colorant_name.string = theICList(sfcolor);
      } else {
        ht_usage.colorant_name.length = 0;
        ht_usage.colorant_name.string = NULL;
      }
      type_name = HTTYPE_to_name(type);
      ht_usage.type_name.length = strlen(type_name);
      ht_usage.type_name.string = (uint8*)type_name;
      ht_usage.token = ++token;
      if ( SwEvent(SWEVT_HT_USAGE_MODULAR, &ht_usage, sizeof(ht_usage)) >= SW_EVENT_ERROR )
        HQTRACE(TRUE,
                ("Error from SwEvent(SWEVT_HT_USAGE_MODULAR, 0x%p, %u)",
                 &ht_usage, sizeof(ht_usage))) ;
    }

    /* Loop over sheet and cache, reporting all colorants for this screen. */

    col_ctx.chptr = chptr; col_ctx.mhtref = mhtref;
    col_ctx.erasenr = sheet_ctx->erasenr;
    col_ctx.ht_colorant.token = token;
    col_ctx.first_one = TRUE;
    for ( i_type = 0 ; i_type <= REPRO_N_TYPES ; ++i_type )
      for ( i_ci = 0 ; i_ci <= COLORANTINDEX_MAX ; ++i_ci )
        col_ctx.colorant_reported[i_type][i_ci] = FALSE;
    (void)gucr_sheetIterateColorants(sheet_ctx->hf,
                                     ht_report_colorant, &col_ctx);
    /* Now the event for the last one. There's always at least one colorant, ci
       from the outer loop. */
    col_ctx.ht_colorant.last_one = TRUE;
    if ( SwEvent(SWEVT_HT_USAGE_COLORANT,
                 &col_ctx.ht_colorant, sizeof(col_ctx.ht_colorant))
         >= SW_EVENT_ERROR )
      HQTRACE(TRUE,
              ("Error from SwEvent(SWEVT_HT_USAGE_COLORANT, 0x%p, %u)",
               &col_ctx.ht_colorant, sizeof(col_ctx.ht_colorant))) ;
  }
  ht_iterChentryEnd(&iter);
  return TRUE;
}


/** Issues screen usage events for all screens used on the given sheet. */
static void ht_report_screen_usage(dl_erase_nr erasenr, GUCR_CHANNEL* hf)
{
  report_screens_struct sheet_ctx;

  if ( !gucr_framesMore(hf) ) /* shortcut */
    return;

  sheet_ctx.erasenr = erasenr; sheet_ctx.hf = hf;
  sheet_ctx.context = get_core_context();
  clear_screen_mark(erasenr);
  (void)gucr_sheetIterateColorants(hf, ht_report_screens_colorant, &sheet_ctx);
}


/** Indicates the screen is used and should not be purged. */
void ht_setUsed(dl_erase_nr erasenr,
                SPOTNO spotno, HTTYPE type, COLORANTINDEX ci)
{
  LISTCHALFTONE *plistch;
  CHALFTONE*    pch;

  HQASSERT(spotno > 0, "Invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(COLORANTINDEX_VALID(ci), "Invalid colorant index");

  plistch = ht_getlistch(spotno, type, ci, NULL);
  HQASSERT(plistch != NULL, "Marking undefined screen used");
  HQASSERT(ht_listch_last_used_dl(plistch) >= erasenr,
           "Marking screen too late");
  pch = ht_listchChalftone(plistch);
  if ( pch != NULL) {
    LEVELSRECORD* plevelsrec = chalftone_io_levels(pch, erasenr);

    HQASSERT(!plevelsrec->preloaded, "Marking screen used after preload");
    UNUSED_PARAM(LEVELSRECORD*, plevelsrec);
    /* The existence of the levels record is enough to preserve the screen. */
  } else {
    Bool res;

    htm_set_used_asserted(res, ht_listchMhtref(plistch), erasenr);
    /** \todo This is probably too late to declare the resources, as this is not
        allowed to fail. - Pekka 2012-12-02 */
    HQASSERT(res, "Failed to get halftone resources");
  }
}


/**
 * Applies ht_setUsed to the colorants in the device raster style.  In the
 * front-end the final set of colorants is not easy to predict (spots converted
 * process, late colour management, etc) and therefore we ensure the halftone is
 * preserved for all the colorants in the device raster style.
 */
void ht_setUsedDeviceRS(dl_erase_nr       erasenr,
                        SPOTNO            spotno,
                        HTTYPE            type,
                        GUCR_RASTERSTYLE *deviceRS)
{
  GUCR_CHANNEL *hf;

  HQASSERT(spotno > 0, "Invalid spot number");
  HQASSERT(!guc_backdropRasterStyle(deviceRS),
           "Expected device raster style");

  for ( hf = gucr_framesStart(deviceRS);
        gucr_framesMore(hf);
        gucr_framesNext(&hf) ) {
    GUCR_COLORANT *hc;

    for ( hc = gucr_colorantsStart (hf);
          gucr_colorantsMore(hc, GUCR_INCLUDING_PIXEL_INTERLEAVED);
          gucr_colorantsNext(&hc) ) {
      const GUCR_COLORANT_INFO *colorantInfo;

      if ( gucr_colorantDescription(hc, &colorantInfo) &&
           colorantInfo->colorantIndex != COLORANTINDEX_UNKNOWN )
        ht_setUsed(erasenr, spotno, type, colorantInfo->colorantIndex);
    }
  }
}


void ht_set_all_levels_used(
  dl_erase_nr       erasenr,
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci)
{
  LISTCHALFTONE *plistch;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(COLORANTINDEX_VALID(ci), "invalid colorant index");

  plistch = ht_getlistch(spotno, type, ci, NULL);
  HQASSERT(plistch != NULL, "Marking undefined screen used");
  HQASSERT(ht_listch_last_used_dl(plistch) >= erasenr,
           "Marking screen too late");
  if ( !ht_listchIsModular(plistch) ) {
    CHALFTONE *pch = ht_listchChalftone(plistch);
    /* Found screen for spot and colorant - may be default */
    LEVELSRECORD* plevelsrec;
    int32 levelsused;
    int32 notones = theINoTones(pch);

    plevelsrec = chalftone_io_levels(pch, erasenr);
    levelsused = plevelsrec->levels_used;
    if ( levelsused < (notones - 1) ) {
      FORM** forms = theICachedForms(plevelsrec);
      COLORVALUE tint;
      corecontext_t *context = get_core_context();

      HQASSERT(!plevelsrec->preloaded, "Can't be marking during rendering");
      for ( tint = 1; tint < notones; ++tint ) {
        if ( forms[tint] == NULL ) {
          if ( pch->formclass != NULL )
            ht_allocatechentry(context, pch, plevelsrec, tint);
          else {
            forms[tint] = FORM_LEVEL_IS_USED;
            ++plevelsrec->levels_used;
          }
          ++levelsused;
        }
      }
      HQASSERT(levelsused == notones - 1, "Different full levels vs tones");
    }
  } else {
    Bool res;

    htm_set_used_asserted(res, ht_listchMhtref(plistch), erasenr);
    /** \todo This is probably too late to declare the resources, as this is not
        allowed to fail. - Pekka 2012-12-02 */
    HQASSERT(res, "Failed to get halftone resources");
  }
}


/* This is like ht_allocateForm, but it only marks one level, to ensure
   the screen is not purged.  The screen is only marked if any tint
   requires it.  Returns whether the screen is marked. */
Bool ht_keep_screen(
  dl_erase_nr   erasenr,
  SPOTNO        spotno,
  HTTYPE        type,
  COLORANTINDEX ci,
  int32         ntints,
  COLORVALUE   *tints,
  int32         stride,
  COLORVALUE    white)
{
  LISTCHALFTONE *plistch;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(COLORANTINDEX_VALID(ci), "invalid colorant index");

  plistch = ht_getlistch( spotno, type, ci, NULL );
  HQASSERT(plistch != NULL, "Marking undefined screen used");
  HQASSERT(ht_listch_last_used_dl(plistch) >= erasenr,
           "Marking screen too late");
  if (!ht_listchIsModular(plistch)) {
    CHALFTONE *pch = ht_listchChalftone(plistch);
    LEVELSRECORD *plevelsrec = chalftone_levels(pch, erasenr);
    if ( plevelsrec != NULL ) { /* if it exists, the screen will keep */
      HQASSERT(!plevelsrec->preloaded, "Marking screen used after preload");
      return TRUE;
    }
    HQASSERT(erasenr == input_dl, "Too late to mark the screen now");
    while ( --ntints >= 0 ) {
      COLORVALUE tint = *tints;
      tints += stride;
      HQASSERT(tint <= white, "tint out of range");
      /* Simulate an ht render (although quantisation here is to contone) */
      if ( tint > 0 && tint < white ) {
        /* Cause a levels record to be created. */
        (void)chalftone_input_levels(pch);
        return TRUE;
      }
    }
    return FALSE;
  } else { /* modular screen */
    Bool res;
    htm_set_used_asserted(res, ht_listchMhtref(plistch), erasenr);
    return res;
  }
}


Bool ht_allocateForm(
  dl_erase_nr       erasenr,
  SPOTNO            spotno,
  HTTYPE            type,
  COLORANTINDEX     ci,
  int32             ntints,
  COLORVALUE        *tints,
  int32             stride,
  corecontext_t*    context)
{
  int32         notones_m1;
  FORM**        forms;
  LISTCHALFTONE *plistch;
  CHALFTONE*    pch;
  LEVELSRECORD* plevelsrec;

  HQASSERT(spotno > 0, "invalid spot number");
  HQASSERT(HTTYPE_VALID(type), "invalid type");
  HQASSERT(COLORANTINDEX_VALID(ci), "invalid colorant index");

  if (!context) {
    context = get_core_context();
  }
  plistch = ht_getlistch( spotno, type, ci, context );
  HQASSERT(plistch != NULL, "Trying to get a form for an undefined screen");
  if ( plistch == NULL )
    return TRUE; /* Should be impossible, but hack to hide marking bug. */
  HQASSERT(ht_listch_last_used_dl(plistch) >= erasenr,
           "Marking screen too late");
  if ( !ht_listchIsModular( plistch) ) {
    pch = ht_listchChalftone(plistch);
    notones_m1 = theINoTones(pch) - 1;
    plevelsrec = chalftone_io_levels(pch, erasenr);
    if ( plevelsrec == NULL )
      return TRUE; /* Should be impossible, but hack to hide marking bug. */
    if ( plevelsrec->levels_used >= notones_m1 )
      return TRUE;
    forms = theICachedForms(plevelsrec);
    if ( plevelsrec->preloaded )
      HT_LOCK_WR_CLAIM(pch);
    while ( (--ntints) >= 0 ) {
      COLORVALUE tint = tints[0];
      tints += stride;
      HQASSERT(tint <= notones_m1 + 1, "tint out of range");

      if ( tint > 0 && tint <= notones_m1 && forms[tint] == NULL ) {
        if ( pch->formclass != NULL ) {
          ht_allocatechentry(context, pch, plevelsrec, tint);
          if ( forms[tint] != NULL && plevelsrec->preloaded ) {
            /* The new form is inserted directly in the forms list, so,
               if ht_preloadchforms has already been called the form
               needs to be initialised immediately. Also, can't use
               regenerate_form since the preload only applies to
               colorants in the current sheet and ht_allocateForm may be
               called with other colorants (any FORM_LEVEL_IS_USED forms
               would confuse regenerate_form). */
            ht_params_t htp;

            HQASSERT(forms[tint] != INVALID_FORM,
                     "Must not use INVALID_FORM after preload");
            htp.listchptr = plistch;
            rawstorechglobals(&htp, pch);
            /* phase will not be used, so don't set it in htp */
            init_form(pch, &htp, forms[tint], tint);
#ifdef LEVEL_TRACKING
            plevelsrec->level_used[tint] = TRUE;
#endif
          }
        } else {
          if ( !plevelsrec->preloaded )
            forms[tint] = FORM_LEVEL_IS_USED;
          ++plevelsrec->levels_used;
        }
        if ( plevelsrec->levels_used >= notones_m1 ) {
          if ( plevelsrec->preloaded )
            HT_LOCK_RELEASE();
          return TRUE;
        }
      }
    } /* while tints */
    if ( plevelsrec->preloaded )
      HT_LOCK_RELEASE();
    return FALSE ;
  } else { /* modular screen */
    Bool res;
    htm_set_used_asserted(res, ht_listchMhtref(plistch), erasenr);
    return res;
  }
} /* Function ht_allocateForm */


void ht_transferLevels(CHALFTONE *chSrc, CHALFTONE *chDst)
{
  int32 scaled_levels = (int32)((float)chSrc->usage->levels_used
                                * chDst->notones / chSrc->notones);

  if ( scaled_levels == 0 && chSrc->usage->levels_used > 0 )
    scaled_levels = 1;
  /* Can't decrease levels, since the caller can't guarantee this
     chDst is only used for the colorant it is manipulating. */
  chDst->usage->levels_used = max(scaled_levels, chDst->usage->levels_used);
}


/*
 * Function:    ht_isPattern
 *
 * Purpose:     Check if screen is pattern
 *
 * Arguments:   spotno    - spot number
 *
 * Returns:     TRUE if screen exists and is for pattern, else FALSE
 *
 * Notes: DON'T USE THIS FUNCTION TO CHECK IF PATTERN SCREENS ARE IN
 * USE, use fTreatScreenAsPattern in pattern.c instead. That the screen
 * is suitable for use as a pattern is not a sufficient condition for
 * treating it as such.
 */
Bool ht_isPattern(
  SPOTNO spotno)
{
  CHALFTONE*  pch;

  HQASSERT(spotno > 0, "ht_isPattern: invalid spot number");

  /* Check for screen details and that it is pattern */
  pch = ht_getch(spotno, HTTYPE_DEFAULT, COLORANTINDEX_NONE);
  return pch != NULL
    && (theITHXfer(pch) == NULL || !HalfGetExtraGrays(pch))
#if 0
    /** \todo ajcd 2009-01-18: What was this here for? It will always return
        FALSE if notones == 2, which is taken care of by the next
        condition. */
    && !ht_use_CT_NORMAL(theINoTones(pch), theIAccurateScreen(pch))
#endif
    && theINoTones(pch) == 2 && pch->formclass == NULL ;
}


Bool ht_chHasFormclass( CHALFTONE* pch )
{
  return pch->formclass != NULL;
}


Bool ht_chIsThreshold( CHALFTONE* pch )
{
  return pch->maxthxfer != 0;
}


Bool ht_chIsNormalThreshold( CHALFTONE* pch )
{
  return pch->thxfer && !HalfGetExtraGrays(pch);
}


Bool ht_chIsUsedForRendering(CHALFTONE* pch)
{
  /* Preload updates pch->forms, so just rely on that. But it might be an old
     one, so check erasenr. */
  return pch->forms != NULL && pch->forms->erasenr == output_dl;
}


uint16 ht_chNoTones( CHALFTONE* pch )
{
  return pch->notones;
}


Bool ht_chAccurate( CHALFTONE* pch )
{
  return pch->accurateScreen;
}


SYSTEMVALUE ht_chAngle(CHALFTONE* pch)
{
  return pch->anglv;
}


void *ht_calibration_screen_info(void *handle,
                                 SPOTNO         startSpotno,
                                 dl_erase_nr    erasenr,
                                 SPOTNO        *spotno,
                                 HTTYPE        *type,
                                 COLORANTINDEX *ci,
                                 Bool          *calibrationWarning,
                                 int32         *levelsUsed,
                                 NAMECACHE    **sfName,
                                 SYSTEMVALUE   *frequency,
                                 NAMECACHE    **sfColor,
                                 uint8         *reportName,
                                 uint32         reportNameLength,
                                 Bool          *screenReported)
{
  CHALFTONE       *chptr;
  LISTCHALFTONE   *plistch;
  MODHTONE_REF *mhtref;
  ht_cacheIterator *iter = handle;
  Bool more;
  SPOTNO c_spotno;
  COLORANTINDEX c_ci;
  HTTYPE c_type;

  if (iter == NULL) {/* The first usage, initialise the iteration. */
    iter = mm_alloc(mm_pool_temp, sizeof(ht_cacheIterator),
                    MM_ALLOC_CLASS_HALFTONE_CACHE);
    if (iter == NULL) {
      HQTRACE(TRUE, ("Ran out of space for calibration check"));
      /* Just give up calibrating */
      *spotno = SPOT_NO_INVALID;
      return NULL;
    }
    ht_iterChentryBegin(iter, erasenr, startSpotno, COLORANTINDEX_UNKNOWN);
  }
  /* Find next valid screen. */
  do {
    more = ht_iterChentryNext(iter, &plistch, &chptr, &mhtref,
                              &c_spotno, &c_type, &c_ci);
    if (!more) {
      *spotno = SPOT_NO_INVALID;
      ht_iterChentryEnd(iter);
      mm_free(mm_pool_temp, iter, sizeof(ht_cacheIterator));
      return NULL;
    }
    /* Ignore the None and All separations (not directly used for
       rendering), and bi-level and pattern screens. */
  } while ( c_ci == COLORANTINDEX_NONE || c_ci == COLORANTINDEX_ALL
            || (chptr != NULL
                && (theINoTones(chptr) < 2 || chptr->formclass == NULL)) );

  *spotno = c_spotno;
  if (c_spotno != startSpotno && handle != NULL) {
    /* reached new spotno (and not initializing), stop */
    ht_iterChentryEnd(iter);
    mm_free(mm_pool_temp, iter, sizeof(ht_cacheIterator));
    return NULL;
  }
  *ci = c_ci; *type = c_type;
  *calibrationWarning = ht_listchCalibrationWarning( plistch );
  if ( chptr != NULL ) {
    LEVELSRECORD *levels = chalftone_levels(chptr, erasenr);
    *levelsUsed = levels != NULL ? levels->levels_used : 0;
  } else
    *levelsUsed = htm_is_used(mhtref, erasenr) ? 1 : 0;
  *sfName = chptr != NULL ? chptr->sfname : ht_listchHalfToneName(plistch);
  /* Return a frequency of -1 to indicate there isn't one. */
  *frequency = chptr != NULL ? (chptr->thxfer != NULL ? -1 : chptr->freqv)
                             : -1;
  *sfColor = ht_listchSFColor( plistch );
  if (reportNameLength > 0) {
    uint8 nambuf[LONGESTSCREENNAME * 2];

    HQASSERT(reportName != NULL, "reportName == NULL");
    ht_dot_shape_string( ht_listchHalfToneName(plistch), *sfName,
                         TRUE, nambuf );
    if (reportNameLength > strlen ((char*) nambuf) + 1)
      reportNameLength = strlen_uint32 ((char*) nambuf) + 1;
    HqMemCpy(reportName, nambuf, reportNameLength);
    reportName[reportNameLength - 1] = 0;
  }
  if ( chptr != NULL ) {
    *screenReported = theIScreenReport( chptr ) ;
    /* As a side effect of this function, mark the screen as reported. */
    theIScreenReport( chptr ) = TRUE ;
  } else {
    *screenReported = mhtref->reported;
    mhtref->reported = TRUE;
  }
  return iter;
}


/* A function to clear the screen reported flags of all screens. Written
 * for the benefit of uncalibrated screen detection to help avoid
 * reporting screens more than once.
 */
void ht_clear_all_screen_reported_flags(dl_erase_nr erasenr)
{
  ht_cacheIterator iter;
  CHALFTONE *chptr ;
  MODHTONE_REF *mhtref;

  ht_iterChentryBegin(&iter, erasenr, SPOT_NO_INVALID, COLORANTINDEX_UNKNOWN);
  while ( ht_iterChentryNext(&iter, NULL, &chptr, &mhtref, NULL, NULL, NULL) )
    if ( chptr != NULL )
      theIScreenReport( chptr ) = FALSE ;
    else
      mhtref->reported = FALSE;
  ht_iterChentryEnd(&iter);
}

/* ========================================================================== */
/* Low level handler to route MONITOR events for the HALFTONE channel to
   the %progress%HalftoneInfo channel, if possible. */

static sw_event_result HQNCALL mon_htinfo(void * context, sw_event * ev)
{
  SWMSG_MONITOR * mon = ev->message ;
  DEVICE_FILEDESCRIPTOR fd ;
  UNUSED_PARAM(void *, context) ;

  if (mon == 0 || ev->length < sizeof(SWMSG_MONITOR) ||
      mon->channel != MON_CHANNEL_HALFTONE ||
      progressdev == NULL)
    return SW_EVENT_CONTINUE ;

  /* The previous implementation opened and closed the file around each
     message, so we shall too. */
  fd = (*theIOpenFile(progressdev))(progressdev, (uint8*)"HalftoneInfo",
                                    SW_WRONLY | SW_CREAT) ;
  if (fd < 0) {
    /* Unable to output to the %progress%HalftoneInfo file. Previous
       implementation returned an error from the ht events in this scenario
       which seems overly harsh. We'll fall back to the underlying monitor
       device instead. */
    return SW_EVENT_CONTINUE ;
  }

  /* We're going to assume we're not getting these from multiple threads, so
     we don't need to mutex writing to the device. It wouldn't have worked
     previously otherwise. */
  (void) (*theIWriteFile(progressdev))(progressdev, fd,
                                       mon->text, (int32)mon->length) ;

  (void)(*theICloseFile(progressdev))(progressdev, fd) ;

  return SW_EVENT_HANDLED ;
}

/*---------------------------------------------------------------------------*/
/* Default event handlers to report screen generation. These handlers are
   installed at low priority so the skin can override them or observe the
   results. */
static sw_event_result HQNCALL ht_generation_search(void *context,
                                                    sw_event *event)
{
  SWMSG_HT_GENERATION_SEARCH *msg = event->message;
  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_GENERATION_SEARCH))
    return SW_EVENT_CONTINUE;

  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTGENSEARCH,
            UVM("Searching for a screen set for frequency: %f\n"),
            msg->frequency);

  return SW_EVENT_CONTINUE;
}


static sw_event_result HQNCALL ht_generation_start(void *context,
                                                   sw_event *event)
{
  SWMSG_HT_GENERATION_START *msg = event->message;
  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_GENERATION_START))
    return SW_EVENT_CONTINUE;

  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTGENSTART,
    UVM("About to generate a screen: Freq: %f, Angle: %f, Spot Function: '%.*s'\n"),
    msg->frequency, msg->angle, msg->spot_name.length, msg->spot_name.string);

  return SW_EVENT_CONTINUE;
}


static sw_event_result HQNCALL ht_generation_end(void *context,
                                                 sw_event *event)
{
  SWMSG_HT_GENERATION_END *msg = event->message;
  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_GENERATION_END))
    return SW_EVENT_CONTINUE;

  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTGENEND,
            UVS("Finished generating screen:\n"));
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTGENEND,
            UVM("    Dot Shape: '%.*s', Frequency: %f, Angle: %f\n"),
            msg->spot_name.length, msg->spot_name.string,
            msg->frequency, msg->angle);
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTGENEND,
            UVM("    Deviated Frequency: %f, Frequency Inacc: %f, Angle Inacc: %f\n"),
            msg->deviated_frequency, msg->frequency_inaccuracy,
            msg->angle_inaccuracy);

  return SW_EVENT_CONTINUE;
}


/* Default event handlers to report screen usage. These handlers are installed
   at low priority so the skin can override them or observe the results. */


static sw_event_result HQNCALL ht_usage_threshold(void *context,
                                                  sw_event *event)
{
  SWMSG_HT_USAGE_THRESHOLD *msg = event->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_USAGE_THRESHOLD))
    return SW_EVENT_CONTINUE;

  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTTHRESHOLD,
            UVM("  Dot shape: '%.*s'"),
            msg->screen_name.length, msg->screen_name.string);
  if ( msg->colorant_name.length > 0 ) {
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTTHRESHOLD,
              UVM("  Component: %.*s"),
              msg->colorant_name.length, msg->colorant_name.string);
    if ( msg->type_name.string[0] != 'D' /* "Default" */ )
      emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTTHRESHOLD,
                (uint8*)"/%.*s",
                msg->type_name.length, msg->type_name.string);
  }
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTTHRESHOLD,
            UVM("\n    Tones used: %u/%u\n"),
            msg->tones_used, msg->tones_total);

  return SW_EVENT_CONTINUE;
}


static sw_event_result HQNCALL ht_usage_spot(void *context, sw_event *event)
{
  SWMSG_HT_USAGE_SPOT *msg = event->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_USAGE_SPOT))
    return SW_EVENT_CONTINUE;

  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTSPOT,
            UVM("  Dot Shape: '%.*s', Frequency: %f, Angle: %f\n"),
            msg->function_name.length, msg->function_name.string,
            msg->frequency, msg->angle);
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTSPOT,
            UVM("    Deviated Frequency: %f, Frequency Inacc: %f, Angle Inacc: %f\n"),
            msg->deviated_frequency, msg->frequency_inaccuracy,
            msg->angle_inaccuracy);
  if ( msg->unoptimized_angle )
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTUNOPTIMIZED,
              UVS("    Warning: Unoptimized screen angle.\n"));
  if ( msg->excessive_inaccuracy )
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTINACCURATE,
              UVS("    Warning: Excessive inaccuracy for frequency/angle.\n"));
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTSPOT,
            UVM("    Tones used: %u/%u\n"),
            msg->tones_used, msg->tones_total);

  return SW_EVENT_CONTINUE;
}


static sw_event_result HQNCALL ht_usage_modular(void *context,
                                                sw_event *event)
{
  SWMSG_HT_USAGE_MODULAR *msg = event->message;

  UNUSED_PARAM(void *, context);

  if ( msg == NULL || event->length < sizeof(SWMSG_HT_USAGE_MODULAR) )
    return SW_EVENT_CONTINUE;

  if ( msg->screen_name.length > 0 )
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTMODULAR,
              UVM("  Dot shape: [%.*s]'%.*s'"),
              msg->module_name.length, msg->module_name.string,
              msg->screen_name.length, msg->screen_name.string);
  else
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTMODULAR,
              UVM("  Dot shape: [%.*s]"),
              msg->module_name.length, msg->module_name.string);
  if ( msg->colorant_name.length > 0 ) {
    emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTMODULAR,
              UVM("  Component: %.*s"),
              msg->colorant_name.length, msg->colorant_name.string);
    if ( msg->type_name.string[0] != 'D' /* "Default" */ )
      emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTMODULAR,
                (uint8*)"/%.*s",
                msg->type_name.length, msg->type_name.string);
  }
  emonitorf(msg->timeline, MON_CHANNEL_HALFTONE, MON_TYPE_HTMODULAR,
            (uint8*)"\n");

  return SW_EVENT_CONTINUE;
}


static sw_event_result HQNCALL ht_usage_colorant(void *context, sw_event *event)
{
  SWMSG_HT_USAGE_COLORANT *msg = event->message;
  static Bool colorant_line_started = FALSE;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || event->length < sizeof(SWMSG_HT_USAGE_COLORANT))
    return SW_EVENT_CONTINUE;

  if ( ! colorant_line_started ) {
    colorant_line_started = TRUE;
    emonitorf(0, MON_CHANNEL_HALFTONE, MON_TYPE_HTCOLORANT,
              UVS("    Colorants:"));
  }
  if ( msg->type_name.string[0] == 'D' /* "Default" */ )
    emonitorf(0, MON_CHANNEL_HALFTONE, MON_TYPE_HTCOLORANT,
              (uint8*)" '%.*s'",
              msg->colorant_name.length, msg->colorant_name.string);
  else
    emonitorf(0, MON_CHANNEL_HALFTONE, MON_TYPE_HTCOLORANT,
              (uint8*)" '%.*s'/%.*s",
              msg->colorant_name.length, msg->colorant_name.string,
              msg->type_name.length, msg->type_name.string);
  if ( msg->last_one ) {
    emonitorf(0, MON_CHANNEL_HALFTONE, MON_TYPE_HTCOLORANT,
              (uint8*)"\n");
    colorant_line_started = FALSE;
  }

  return SW_EVENT_CONTINUE;
}


static sw_event_handlers handlers[] = {
  { ht_generation_search, NULL, 0, SWEVT_HT_GENERATION_SEARCH, SW_EVENT_DEFAULT },
  { ht_generation_start,  NULL, 0, SWEVT_HT_GENERATION_START,  SW_EVENT_DEFAULT },
  { ht_generation_end,    NULL, 0, SWEVT_HT_GENERATION_END,    SW_EVENT_DEFAULT },
  { ht_usage_threshold,   NULL, 0, SWEVT_HT_USAGE_THRESHOLD,   SW_EVENT_DEFAULT },
  { ht_usage_spot,        NULL, 0, SWEVT_HT_USAGE_SPOT,        SW_EVENT_DEFAULT },
  { ht_usage_modular,     NULL, 0, SWEVT_HT_USAGE_MODULAR,     SW_EVENT_DEFAULT },
  { ht_usage_colorant,    NULL, 0, SWEVT_HT_USAGE_COLORANT,    SW_EVENT_DEFAULT },
  { mon_htinfo,           NULL, 0, SWEVT_MONITOR,              SW_EVENT_DEFAULT },
} ;


/*---------------------------------------------------------------------------*/

/** File runtime initialisation */
static void init_C_globals_halftone(void)
{
  FORM initform = { 0 } ;
  invalidform = deferredform = initform ;
  deferring_allocation = 0 ;
  formclasses = NULL ;
  gNumDeferred = 0;
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  installing_hexhds = FALSE;
#endif
  theseed = 0;
  oldest_dl = FIRST_DL; output_dl = input_dl = INVALID_DL;
  ht_form_keep = TRUE;
  in_rip_ht_used_last_dl = INVALID_DL;
#ifdef METRICS_BUILD
  halftone_metrics_reset(SW_METRICS_RESET_BOOT) ;
  sw_metrics_register(&halftone_metrics_hook) ;
#endif
  ht_form_pool = NULL;
}


static Bool halftone_swstart(SWSTART *params)
{
  UNUSED_PARAM(SWSTART *, params) ;

  if ( SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) != SW_RDR_SUCCESS )
    return FAILURE(FALSE) ;

#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  /* Provide a way of flagging that we're installing a hexachrome HDS
     screen, so an appropriate protection flag can be set in the cache. */
  register_ripvar(NAME_installing_hexhds, OBOOLEAN, (void*)&installing_hexhds);
#endif
  multi_rwlock_init(&ht_lock, HT_LOCK_INDEX, SW_TRACE_HT_ACQUIRE,
                    SW_TRACE_HT_READ_HOLD, SW_TRACE_HT_WRITE_HOLD);
  multi_mutex_init(&formclasses_mutex, HT_FORMCLASS_INDEX, FALSE,
                   SW_TRACE_HT_FORM_ACQUIRE, SW_TRACE_HT_FORM_HOLD);

  if ( mm_pool_create(&ht_form_pool, HTFORM_POOL_TYPE, MM_SEGMENT_SIZE,
                      (size_t)256, (size_t)8) != MM_SUCCESS )
    return FAILURE(FALSE);

  return initHalfToneCache() ;
}


static void halftone_finish(void)
{
  finishHalfToneCache();
  multi_mutex_finish(&formclasses_mutex);
  multi_rwlock_finish(&ht_lock);

  mm_pool_destroy(ht_form_pool);
  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;
}


/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( gu_misc )
IMPORT_INIT_C_GLOBALS( gu_prscn )
IMPORT_INIT_C_GLOBALS( hpscreen )
IMPORT_INIT_C_GLOBALS( htcache )

/** Compound runtime initialisation */
void halftone_C_globals(core_init_fns *fns)
{
  init_C_globals_gu_misc() ;
  init_C_globals_gu_prscn() ;
  init_C_globals_halftone() ;
  init_C_globals_hpscreen() ;
  init_C_globals_htcache() ;

  fns->swstart = halftone_swstart ;
  fns->finish = halftone_finish ;
}

/*
Log stripped */
