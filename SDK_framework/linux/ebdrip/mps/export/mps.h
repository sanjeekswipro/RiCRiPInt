/* $Id: export:mps.h,v 1.100.1.1.1.1 2013/12/19 11:27:03 anon Exp $
 * $HopeName: SWmps!export:mps.h(EBDSDK_P.1) $
 */

/* impl.h.mps: RAVENBROOK MEMORY POOL SYSTEM C INTERFACE
 *
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 *
 * .readership: customers, MPS developers.
 * .sources: design.mps.interface.c.
 */

#ifndef mps_h
#define mps_h

#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#if _XOPEN_SOURCE >= 500 && !defined(_MSC_VER)
#if defined(Solaris)
#include <inttypes.h>
#else
#include <stdint.h>
#endif
#endif

/* Macros */

/*
 * MPS_CALL is used to annotate for the calling convention on functions that
 * form the library's external API.
 *
 * Typically, a definition would be passed through from the make system. This
 * is just a default definition, whereby the calling convention is implicit.
 *
 */
#ifndef MPS_CALL
#define MPS_CALL
#endif

/* Abstract Types */

typedef struct mps_arena_s  *mps_arena_t;  /* arena */
typedef struct mps_arena_class_s *mps_arena_class_t;  /* arena class */
typedef struct mps_pool_s   *mps_pool_t;   /* pool */
typedef struct mps_chain_s  *mps_chain_t;  /* chain */
typedef struct mps_fmt_s    *mps_fmt_t;    /* object format */
typedef struct mps_root_s   *mps_root_t;   /* root */
typedef struct mps_class_s  *mps_class_t;  /* pool class */
typedef struct mps_thr_s    *mps_thr_t;    /* thread registration */
typedef struct mps_ap_s     *mps_ap_t;     /* allocation point */
typedef struct mps_ld_s     *mps_ld_t;     /* location dependency */
typedef struct mps_ss_s     *mps_ss_t;     /* scan state */
typedef struct mps_message_s
  *mps_message_t;                          /* message */
typedef struct mps_alloc_pattern_s
  *mps_alloc_pattern_t;                    /* allocation patterns */
typedef struct mps_frame_s
  *mps_frame_t;                            /* allocation frames */

/* Concrete Types */

#if defined(_C99) || _XOPEN_SOURCE >= 500 || _MSC_VER >= 1300
typedef uintptr_t mps_word_t; /* pointer-sized word */
#else
typedef unsigned long mps_word_t;
#endif
typedef int mps_bool_t;         /* boolean (int) */
typedef int mps_res_t;          /* result code (int) */
typedef unsigned mps_shift_t;   /* shift amount (unsigned int) */
typedef void *mps_addr_t;       /* managed address (void *) */
typedef size_t mps_align_t;     /* alignment (size_t) */
typedef unsigned mps_rm_t;      /* root mode (unsigned) */
typedef unsigned mps_rank_t;    /* ranks (unsigned) */
typedef unsigned mps_message_type_t;    /* message type (unsigned) */

/* Result Codes */
/* .result-codes: Keep in sync with impl.h.mpmtypes.result-codes */
/* and the check in impl.c.mpsi.check.rc */

enum {
  MPS_RES_OK = 0,               /* success (always zero) */
  MPS_RES_FAIL,                 /* unspecified failure */
  MPS_RES_RESOURCE,             /* unable to obtain resources */
  MPS_RES_MEMORY,               /* unable to obtain memory */
  MPS_RES_LIMITATION,           /* built-in limitation reached */
  MPS_RES_UNIMPL,               /* unimplemented facility */
  MPS_RES_IO,                   /* system I/O error */
  MPS_RES_COMMIT_LIMIT,         /* arena commit limit exceeded */
  MPS_RES_PARAM                 /* illegal user parameter value */
};

/* .message.types: Keep in sync with impl.h.mpmtypes.message.types */
/* Not meant to be used by clients, they should use the macros below. */
enum {
  MPS_MESSAGE_TYPE_FINALIZATION,
  MPS_MESSAGE_TYPE_GC
};

/* Message Types
 * This is what clients should use. */
#define mps_message_type_finalization() MPS_MESSAGE_TYPE_FINALIZATION
#define mps_message_type_gc() MPS_MESSAGE_TYPE_GC


/* Reference Ranks
 *
 * See protocol.mps.reference. */

extern mps_rank_t MPS_CALL mps_rank_ambig(void);
extern mps_rank_t MPS_CALL mps_rank_exact(void);
extern mps_rank_t MPS_CALL mps_rank_weak(void);

/* These upper case symbolic forms are obsolescent. */
/* Provided for source compatibility only. */
#define MPS_RANK_AMBIG mps_rank_ambig()
#define MPS_RANK_EXACT mps_rank_exact()
#define MPS_RANK_WEAK mps_rank_weak()

/* Root Modes */
/* .rm: Keep in sync with impl.h.mpmtypes.rm */

#define MPS_RM_CONST    (((mps_rm_t)1<<0))
#define MPS_RM_PROT     (((mps_rm_t)1<<1))


/* Allocation Point */
/* .ap: Keep in sync with impl.h.mpmst.ap. */

typedef struct mps_ap_s {       /* allocation point descriptor */
  mps_addr_t init;              /* limit of initialized memory */
  mps_addr_t alloc;             /* limit of allocated memory */
  mps_addr_t limit;             /* limit of available memory */
  mps_addr_t frameptr;          /* lightweight frame pointer */
  mps_bool_t enabled;           /* lightweight frame status */
  mps_bool_t lwpoppending;      /* lightweight pop pending? */
} mps_ap_s;


/* Segregated-fit Allocation Caches */
/* .sac: Keep in sync with impl.h.sac. */

typedef struct mps_sac_s *mps_sac_t;

#define MPS_SAC_CLASS_LIMIT ((size_t)8)

typedef struct mps_sac_freelist_block_s {
  size_t mps_size;
  size_t mps_count;
  size_t mps_count_max;
  mps_addr_t mps_blocks;
} mps_sac_freelist_block_s;

typedef struct mps_sac_s {
  size_t mps_middle;
  mps_bool_t mps_trapped;
  mps_sac_freelist_block_s mps_freelists[2 * MPS_SAC_CLASS_LIMIT];
} mps_sac_s;

/* .sacc: Keep in sync with impl.h.sac. */
typedef struct mps_sac_class_s {
  size_t mps_block_size;
  size_t mps_cached_count;
  unsigned mps_frequency;
} mps_sac_class_s;

#define mps_sac_classes_s mps_sac_class_s


/* Location Dependency */
/* .ld: Keep in sync with impl.h.mpmst.ld.struct. */

typedef struct mps_ld_s {       /* location dependency descriptor */
  mps_word_t w0, w1;
} mps_ld_s;


/* Format and Root Method Types */
/* .fmt-methods: Keep in sync with impl.h.mpmtypes.fmt-methods */
/* .root-methods: Keep in sync with impl.h.mpmtypes.root-methods */

typedef mps_res_t (MPS_CALL *mps_root_scan_t)(mps_ss_t, void *, size_t);
typedef mps_res_t (MPS_CALL *mps_fmt_scan_t)(mps_ss_t, mps_addr_t, mps_addr_t);
typedef mps_res_t (MPS_CALL *mps_reg_scan_t)(mps_ss_t, mps_thr_t,
                                    void *, size_t);
typedef mps_addr_t (MPS_CALL *mps_fmt_skip_t)(mps_addr_t);
typedef void (MPS_CALL *mps_fmt_copy_t)(mps_addr_t, mps_addr_t);
typedef void (MPS_CALL *mps_fmt_fwd_t)(mps_addr_t, mps_addr_t);
typedef mps_addr_t (MPS_CALL *mps_fmt_isfwd_t)(mps_addr_t);
typedef void (MPS_CALL *mps_fmt_pad_t)(mps_addr_t, size_t);
typedef mps_addr_t (MPS_CALL *mps_fmt_class_t)(mps_addr_t);


/* Scan State */

typedef struct mps_ss_s {
  void (MPS_CALL *fix)(mps_ss_t, mps_addr_t *);
  mps_word_t w0, w1, w2, w3;
} mps_ss_s;


/* Format Variants */

typedef struct mps_fmt_A_s {
  mps_align_t     align;
  mps_fmt_scan_t  scan;
  mps_fmt_skip_t  skip;
  mps_fmt_copy_t  copy;
  mps_fmt_fwd_t   fwd;
  mps_fmt_isfwd_t isfwd;
  mps_fmt_pad_t   pad;
} mps_fmt_A_s;
typedef struct mps_fmt_A_s *mps_fmt_A_t;  /* deprecated */

typedef struct mps_fmt_B_s {
  mps_align_t     align;
  mps_fmt_scan_t  scan;
  mps_fmt_skip_t  skip;
  mps_fmt_copy_t  copy;
  mps_fmt_fwd_t   fwd;
  mps_fmt_isfwd_t isfwd;
  mps_fmt_pad_t   pad;
  mps_fmt_class_t mps_class;
} mps_fmt_B_s;
typedef struct mps_fmt_B_s *mps_fmt_B_t;  /* deprecated */


typedef struct mps_fmt_auto_header_s {
  mps_align_t     align;
  mps_fmt_scan_t  scan;
  mps_fmt_skip_t  skip;
  mps_fmt_fwd_t   fwd;
  mps_fmt_isfwd_t isfwd;
  mps_fmt_pad_t   pad;
  size_t          mps_headerSize;
} mps_fmt_auto_header_s;


typedef struct mps_fmt_fixed_s {
  mps_align_t     align;
  mps_fmt_scan_t  scan;
  mps_fmt_fwd_t   fwd;
  mps_fmt_isfwd_t isfwd;
  mps_fmt_pad_t   pad;
} mps_fmt_fixed_s;


/* Internal Definitions */

#define MPS_BEGIN       do {
#define MPS_END         } while(0)
/* MPS_END might cause compiler warnings about constant conditionals.
 * This could be avoided with some loss of efficiency by replacing 0
 * with a variable always guaranteed to be 0.  In Visual C, the
 * warning can be turned off using:
 * #pragma warning(disable: 4127)
 */


/* arenas */

extern void MPS_CALL mps_arena_clamp(mps_arena_t);
extern void MPS_CALL mps_arena_release(mps_arena_t);
extern void MPS_CALL mps_arena_park(mps_arena_t);
extern mps_res_t MPS_CALL mps_arena_collect(mps_arena_t);

extern mps_res_t MPS_CALL mps_arena_create(mps_arena_t *, mps_arena_class_t, ...);
extern mps_res_t MPS_CALL mps_arena_create_v(mps_arena_t *, mps_arena_class_t,
                                            va_list);
extern void MPS_CALL mps_arena_destroy(mps_arena_t);
extern void MPS_CALL mps_arena_abort(mps_arena_t);

extern size_t MPS_CALL mps_arena_reserved(mps_arena_t);
extern size_t MPS_CALL mps_arena_committed(mps_arena_t);
extern size_t MPS_CALL mps_arena_committed_max(mps_arena_t);
extern size_t MPS_CALL mps_arena_spare_committed(mps_arena_t);

extern size_t MPS_CALL mps_arena_commit_limit(mps_arena_t);
extern mps_res_t MPS_CALL mps_arena_commit_limit_set(mps_arena_t, size_t);
extern void MPS_CALL mps_arena_spare_commit_limit_set(mps_arena_t, size_t);
extern size_t MPS_CALL mps_arena_spare_commit_limit(mps_arena_t);

extern mps_bool_t MPS_CALL mps_arena_has_addr(mps_arena_t, mps_addr_t);

/* Client memory arenas */
extern mps_res_t MPS_CALL mps_arena_extend(mps_arena_t, mps_addr_t, size_t);
extern mps_res_t MPS_CALL mps_arena_retract(mps_arena_t, mps_addr_t, size_t);


/* Object Formats */

extern mps_res_t MPS_CALL mps_fmt_create_A(mps_fmt_t *, mps_arena_t,
                                           mps_fmt_A_s *);
extern mps_res_t MPS_CALL mps_fmt_create_B(mps_fmt_t *, mps_arena_t,
                                           mps_fmt_B_s *);
extern mps_res_t MPS_CALL mps_fmt_create_auto_header(mps_fmt_t *, mps_arena_t,
                                                     mps_fmt_auto_header_s *);
extern mps_res_t MPS_CALL mps_fmt_create_fixed(mps_fmt_t *, mps_arena_t,
                                               mps_fmt_fixed_s *);
extern void MPS_CALL mps_fmt_destroy(mps_fmt_t);


/* Pools */

extern mps_res_t MPS_CALL mps_pool_create(mps_pool_t *, mps_arena_t,
                                          mps_class_t, ...);
extern mps_res_t MPS_CALL mps_pool_create_v(mps_pool_t *, mps_arena_t,
                                            mps_class_t, va_list);
extern void MPS_CALL mps_pool_destroy(mps_pool_t);

extern mps_bool_t MPS_CALL mps_pool_has_addr(mps_pool_t, mps_addr_t);

extern void MPS_CALL mps_pool_size(mps_pool_t mps_pool,
                                   size_t *managed_size, size_t *free_size);


typedef struct mps_gen_param_s {
  size_t mps_capacity;
  double mps_mortality;
} mps_gen_param_s;

extern mps_res_t MPS_CALL mps_chain_create(mps_chain_t *, mps_arena_t,
                                           size_t, mps_gen_param_s *);
extern void MPS_CALL mps_chain_destroy(mps_chain_t);


typedef struct {
  mps_word_t location;
  mps_word_t mps_class;
  /* client-defined fields allowed here */
} mps_debug_info_s;

extern mps_res_t (MPS_CALL mps_alloc)(mps_addr_t *, mps_pool_t, size_t);
extern mps_res_t MPS_CALL mps_alloc_debug(mps_addr_t *, mps_pool_t, size_t,
                                          mps_debug_info_s *);
extern mps_res_t MPS_CALL mps_alloc_debug2(mps_addr_t *, mps_pool_t, size_t,
                                          char *, mps_word_t);
extern void (MPS_CALL mps_free)(mps_pool_t, mps_addr_t, size_t);

extern void mps_pool_clear(mps_pool_t);


/* Allocation Points */

extern mps_res_t MPS_CALL mps_ap_create(mps_ap_t *, mps_pool_t, ...);
extern mps_res_t MPS_CALL mps_ap_create_v(mps_ap_t *, mps_pool_t, va_list);
extern void MPS_CALL mps_ap_destroy(mps_ap_t);

extern mps_res_t (MPS_CALL mps_reserve)(mps_addr_t *, mps_ap_t, size_t);
extern mps_bool_t (MPS_CALL mps_commit)(mps_ap_t, mps_addr_t, size_t);

extern mps_res_t MPS_CALL mps_ap_fill(mps_addr_t *, mps_ap_t, size_t);
extern mps_res_t MPS_CALL mps_ap_fill_with_reservoir_permit(mps_addr_t *,
                                                            mps_ap_t,
                                                            size_t);

extern mps_res_t (MPS_CALL mps_ap_frame_push)(mps_frame_t *, mps_ap_t);
extern mps_res_t (MPS_CALL mps_ap_frame_pop)(mps_ap_t, mps_frame_t);

extern mps_bool_t MPS_CALL mps_ap_trip(mps_ap_t, mps_addr_t, size_t);

extern mps_alloc_pattern_t MPS_CALL mps_alloc_pattern_ramp(void);
extern mps_alloc_pattern_t MPS_CALL mps_alloc_pattern_ramp_collect_all(void);
extern mps_res_t MPS_CALL mps_ap_alloc_pattern_begin(mps_ap_t, mps_alloc_pattern_t);
extern mps_res_t MPS_CALL mps_ap_alloc_pattern_end(mps_ap_t, mps_alloc_pattern_t);
extern mps_res_t MPS_CALL mps_ap_alloc_pattern_reset(mps_ap_t);


/* Segregated-fit Allocation Caches */

extern mps_res_t MPS_CALL mps_sac_create(mps_sac_t *, mps_pool_t, size_t,
                                         mps_sac_classes_s *);
extern void MPS_CALL mps_sac_destroy(mps_sac_t);
extern mps_res_t MPS_CALL mps_sac_alloc(mps_addr_t *, mps_sac_t, size_t,
                                        mps_bool_t);
extern void MPS_CALL mps_sac_free(mps_sac_t, mps_addr_t, size_t);
extern void MPS_CALL mps_sac_flush(mps_sac_t);
extern size_t MPS_CALL mps_sac_free_size(mps_sac_t);

/* Direct access to mps_sac_fill and mps_sac_empty is not supported. */
extern mps_res_t MPS_CALL mps_sac_fill(mps_addr_t *, mps_sac_t, size_t, mps_bool_t);
extern void MPS_CALL mps_sac_empty(mps_sac_t, mps_addr_t, size_t);

#define MPS_SAC_ALLOC_FAST(res_o, p_o, sac, size, has_reservoir_permit) \
  MPS_BEGIN \
    size_t _mps_i, _mps_s; \
    \
    _mps_s = (size); \
    if (_mps_s > (sac)->mps_middle) { \
      _mps_i = 0; \
      while (_mps_s > (sac)->mps_freelists[_mps_i].mps_size) \
        _mps_i += 2; \
    } else { \
      _mps_i = 1; \
      while (_mps_s <= (sac)->mps_freelists[_mps_i].mps_size) \
        _mps_i += 2; \
    } \
    if ((sac)->mps_freelists[_mps_i].mps_count != 0) { \
      (p_o) = (sac)->mps_freelists[_mps_i].mps_blocks; \
      (sac)->mps_freelists[_mps_i].mps_blocks = *(mps_addr_t *)(p_o); \
      --(sac)->mps_freelists[_mps_i].mps_count; \
      (res_o) = MPS_RES_OK; \
    } else \
      (res_o) = mps_sac_fill(&(p_o), sac, _mps_s, \
                             has_reservoir_permit); \
  MPS_END

#define MPS_SAC_FREE_FAST(sac, p, size) \
  MPS_BEGIN \
    size_t _mps_i, _mps_s; \
    \
    _mps_s = (size); \
    if (_mps_s > (sac)->mps_middle) { \
      _mps_i = 0; \
      while (_mps_s > (sac)->mps_freelists[_mps_i].mps_size) \
        _mps_i += 2; \
    } else { \
      _mps_i = 1; \
      while (_mps_s <= (sac)->mps_freelists[_mps_i].mps_size) \
        _mps_i += 2; \
    } \
    if ((sac)->mps_freelists[_mps_i].mps_count \
        < (sac)->mps_freelists[_mps_i].mps_count_max) { \
       *(mps_addr_t *)(p) = (sac)->mps_freelists[_mps_i].mps_blocks; \
      (sac)->mps_freelists[_mps_i].mps_blocks = (p); \
      ++(sac)->mps_freelists[_mps_i].mps_count; \
    } else \
      mps_sac_empty(sac, p, _mps_s); \
  MPS_END

/* deprecated, retained for backward compatibility */
#define MPS_SAC_ALLOC(res_o, p_o, sac, size, has_reservoir_permit) \
      MPS_SAC_ALLOC_FAST(res_o, p_o, sac, size, has_reservoir_permit)
#define MPS_SAC_FREE(sac, p, size) MPS_SAC_FREE_FAST(sac, p, size)


/* Low memory reservoir */

extern void MPS_CALL mps_reservoir_limit_set(mps_arena_t, size_t);
extern size_t MPS_CALL mps_reservoir_limit(mps_arena_t);
extern size_t MPS_CALL mps_reservoir_available(mps_arena_t);
extern mps_res_t MPS_CALL mps_reserve_with_reservoir_permit(mps_addr_t *,
                                                            mps_ap_t,
                                                            size_t);


/* Reserve Macros */
/* .reserve: Keep in sync with impl.c.buffer.reserve. */

#define mps_reserve(_p_o, _mps_ap, _size) \
  ((char *)(_mps_ap)->alloc + (_size) > (char *)(_mps_ap)->alloc && \
   (char *)(_mps_ap)->alloc + (_size) <= (char *)(_mps_ap)->limit ? \
     ((_mps_ap)->alloc = \
       (mps_addr_t)((char *)(_mps_ap)->alloc + (_size)), \
      *(_p_o) = (_mps_ap)->init, \
      MPS_RES_OK) : \
     mps_ap_fill(_p_o, _mps_ap, _size))


#define MPS_RESERVE_BLOCK(_res_v, _p_v, _mps_ap, _size) \
  MPS_BEGIN \
    char *_alloc = (char *)(_mps_ap)->alloc; \
    char *_next = _alloc + (_size); \
    if(_next > _alloc && _next <= (char *)(_mps_ap)->limit) { \
      (_mps_ap)->alloc = (mps_addr_t)_next; \
      (_p_v) = (_mps_ap)->init; \
      (_res_v) = MPS_RES_OK; \
    } else \
      (_res_v) = mps_ap_fill(&(_p_v), _mps_ap, _size); \
  MPS_END


#define MPS_RESERVE_WITH_RESERVOIR_PERMIT_BLOCK(_res_v, _p_v, _mps_ap, _size) \
  MPS_BEGIN \
    char *_alloc = (char *)(_mps_ap)->alloc; \
    char *_next = _alloc + (_size); \
    if(_next > _alloc && _next <= (char *)(_mps_ap)->limit) { \
      (_mps_ap)->alloc = (mps_addr_t)_next; \
      (_p_v) = (_mps_ap)->init; \
      (_res_v) = MPS_RES_OK; \
    } else \
      (_res_v) = mps_ap_fill_with_reservoir_permit(&(_p_v), _mps_ap, _size); \
  MPS_END


/* Commit Macros */
/* .commit: Keep in sync with impl.c.buffer.commit. */

#define mps_commit(_mps_ap, _p, _size) \
  ((_mps_ap)->init = (_mps_ap)->alloc, \
   (_mps_ap)->limit != 0 || mps_ap_trip(_mps_ap, _p, _size))


/* Root Creation and Destruction */

extern mps_res_t MPS_CALL mps_root_create(mps_root_t *, mps_arena_t, mps_rank_t,
                                          mps_rm_t, mps_root_scan_t,
                                          void *, size_t);
extern mps_res_t MPS_CALL mps_root_create_table(mps_root_t *, mps_arena_t,
                                                mps_rank_t, mps_rm_t,
                                                mps_addr_t *, size_t);
extern mps_res_t MPS_CALL mps_root_create_table_masked(mps_root_t *, mps_arena_t,
                                                       mps_rank_t, mps_rm_t,
                                                       mps_addr_t *, size_t,
                                                       mps_word_t);
extern mps_res_t MPS_CALL mps_root_create_fmt(mps_root_t *, mps_arena_t,
                                              mps_rank_t, mps_rm_t,
                                              mps_fmt_scan_t, mps_addr_t,
                                              mps_addr_t);
extern mps_res_t MPS_CALL mps_root_create_reg(mps_root_t *, mps_arena_t,
                                              mps_rank_t, mps_rm_t, mps_thr_t,
                                              mps_reg_scan_t, void *, size_t);
extern void MPS_CALL mps_root_destroy(mps_root_t);

extern mps_res_t MPS_CALL mps_stack_scan_ambig(mps_ss_t, mps_thr_t,
                                               void *, size_t);


/* Protection Trampoline and Thread Registration */

typedef void *(MPS_CALL *mps_tramp_t)(void *, size_t);
extern void (MPS_CALL mps_tramp)(void **, mps_tramp_t, void *, size_t);

extern mps_res_t MPS_CALL mps_thread_reg(mps_thr_t *, mps_arena_t);
extern void MPS_CALL mps_thread_dereg(mps_thr_t);


/* Location Dependency */

extern void MPS_CALL mps_ld_reset(mps_ld_t, mps_arena_t);
extern void MPS_CALL mps_ld_add(mps_ld_t, mps_arena_t, mps_addr_t);
extern void MPS_CALL mps_ld_merge(mps_ld_t, mps_arena_t, mps_ld_t);
extern mps_bool_t MPS_CALL mps_ld_isstale(mps_ld_t, mps_arena_t, mps_addr_t);

extern mps_word_t MPS_CALL mps_collections(mps_arena_t);


/* Messages */

extern mps_bool_t MPS_CALL mps_message_poll(mps_arena_t);
extern void MPS_CALL mps_message_type_enable(mps_arena_t, mps_message_type_t);
extern void MPS_CALL mps_message_type_disable(mps_arena_t, mps_message_type_t);
extern mps_bool_t MPS_CALL mps_message_get(mps_message_t *,
                                           mps_arena_t, mps_message_type_t);
extern void MPS_CALL mps_message_discard(mps_arena_t, mps_message_t);
extern mps_bool_t MPS_CALL mps_message_queue_type(mps_message_type_t *,
                                                  mps_arena_t);
extern mps_message_type_t MPS_CALL mps_message_type(mps_arena_t, mps_message_t);

/* Message Type Specific Methods */

/* MPS_MESSAGE_TYPE_FINALIZATION */

extern void MPS_CALL mps_message_finalization_ref(mps_addr_t *,
                                                  mps_arena_t, mps_message_t);

/* MPS_MESSAGE_TYPE_GC */

extern size_t MPS_CALL mps_message_gc_live_size(mps_arena_t, mps_message_t);

extern size_t MPS_CALL mps_message_gc_condemned_size(mps_arena_t, mps_message_t);

extern size_t MPS_CALL mps_message_gc_not_condemned_size(mps_arena_t,
                                                         mps_message_t);


/* Finalization */

extern mps_res_t MPS_CALL mps_finalize(mps_arena_t, mps_addr_t *);
extern mps_res_t MPS_CALL mps_definalize(mps_arena_t, mps_addr_t *);


/* Telemetry */

extern mps_word_t MPS_CALL mps_telemetry_control(mps_word_t, mps_word_t);
extern mps_word_t MPS_CALL mps_telemetry_intern(const char *);
extern mps_word_t MPS_CALL mps_telemetry_intern_length(const char *, size_t);
extern void MPS_CALL mps_telemetry_label(mps_addr_t, mps_word_t);
extern void MPS_CALL mps_telemetry_flush(void);


/* Heap Walking */

typedef void (MPS_CALL *mps_formatted_objects_stepper_t)(mps_addr_t, mps_fmt_t,
                                                mps_pool_t,
                                                void *, size_t);
extern void MPS_CALL mps_arena_formatted_objects_walk(mps_arena_t,
                                                      mps_formatted_objects_stepper_t,
                                                      void *, size_t);


/* Root Walking */

typedef void (MPS_CALL *mps_roots_stepper_t)(mps_addr_t *, mps_root_t,
                                             void *, size_t);
extern void MPS_CALL mps_arena_roots_walk(mps_arena_t,
                                          mps_roots_stepper_t,
                                          void *, size_t);


/* Pool walking */

typedef void (MPS_CALL *mps_tag_stepper_t)(mps_addr_t *, size_t, mps_fmt_t,
                                           mps_pool_t, mps_debug_info_s *,
                                           void *);

extern void MPS_CALL mps_pool_debug_walk(mps_pool_t, mps_tag_stepper_t, void *);


/* Allocation debug options */


typedef struct mps_pool_debug_option_s {
  void*  fence_template;
  size_t fence_size;
  void*  free_template;
  size_t free_size;
  int    keepTags;
  size_t debug_info_size;
} mps_pool_debug_option_s;

extern void MPS_CALL mps_pool_check_fenceposts(mps_pool_t);
extern void MPS_CALL mps_pool_check_free_space(mps_pool_t);


/* Scanner Support */

/* mps_fix is deprecated */
extern mps_res_t MPS_CALL mps_fix(mps_ss_t, mps_addr_t *);
/* mps_resolve should only be called through MPS_IS_RETAINED */
extern mps_bool_t MPS_CALL mps_resolve(mps_ss_t, mps_addr_t *);
/* mps_scan_poke should only be called through MPS_SCAN_UPDATE_OTHER */
extern void MPS_CALL mps_scan_poke(mps_ss_t, mps_addr_t, mps_addr_t);

#define MPS_SCAN_BEGIN(ss) \
  MPS_BEGIN \
    mps_ss_t _mps_ss = (ss); \
    mps_word_t _mps_w0 = _mps_ss->w0; \
    mps_word_t _mps_w1 = _mps_ss->w1; \
    mps_word_t _mps_w2 = _mps_ss->w2; \
    mps_word_t _mps_wt; \
    {

/* deprecated, only for use by other macros */
#define MPS_FIX1(ss, ref) \
  (_mps_wt = (mps_word_t)1 << ((mps_word_t)(ref) >> _mps_w0 \
                              & (sizeof(mps_word_t) * CHAR_BIT - 1)), \
   _mps_w2 |= _mps_wt, \
   _mps_w1 & _mps_wt)

/* deprecated */
#define MPS_FIX2(ss, ref_io) \
  ((*(ss)->fix)(ss, ref_io), MPS_RES_OK)

/* deprecated */
#define MPS_FIX12(ss, ref_io) \
  (MPS_FIX1(ss, *(ref_io)) ? \
   MPS_FIX2(ss, ref_io) : MPS_RES_OK)

/* deprecated */
#define MPS_FIX(ss, ref_io) MPS_FIX12(ss, ref_io)

#define MPS_RETAIN(ref_io, cond) \
  MPS_BEGIN \
    if (MPS_FIX1(_mps_ss, *(ref_io)) && (cond)) \
      (*_mps_ss->fix)(_mps_ss, (mps_addr_t *)(char *)(ref_io));         \
  MPS_END

#define MPS_IS_RETAINED(ref_io, cond) \
  (!MPS_FIX1(_mps_ss, *(ref_io)) || !(cond) || mps_resolve(_mps_ss, (mps_addr_t *)(char *)(ref_io)))

#define MPS_SCAN_UPDATE(location, value) \
  (_mps_wt = (mps_word_t)1 << ((mps_word_t)(value) >> _mps_w0 \
                              & (sizeof(mps_word_t) * CHAR_BIT - 1)), \
   _mps_ss->w3 |= _mps_wt, \
   (location) = (value))

#define MPS_SCAN_UPDATE_OTHER(location, value) \
  mps_scan_poke(_mps_ss, (mps_addr_t)&(location), (mps_addr_t)(value))

#define MPS_SCAN_CALL(call) \
  MPS_BEGIN \
    (call); _mps_w2 |= _mps_ss->w2; \
  MPS_END

/* deprecated */
#define MPS_FIX_CALL(ss, call) MPS_SCAN_CALL(call)

#define MPS_SCAN_END(ss) \
   } \
   (ss)->w2 = _mps_w2; \
  MPS_END


#endif /* mps_h */
