/* impl.h.fmtpstst: POSTSCRIPT OBJECT FORMAT TEST VERSION
 *
 * $Id: fmtpstst.h,v 1.9.11.1.1.1 2013/12/19 11:27:08 anon Exp $
 * $HopeName: MMsrc!fmtpstst.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * .readership: MPS developers, SW developers
 */

#ifndef fmtpstst_h
#define fmtpstst_h

#include "mps.h"
#include "fmtpscon.h"


/* SIZE_ALIGN_UP defined here for convenience */
#define SIZE_ALIGN_UP(w, a) (((w) + (a) - 1) & ~((size_t)(a) - 1))


extern mps_fmt_fixed_s *ps_fmt_fixed(void);
extern mps_fmt_A_s *ps_fmt_typed(void);

extern mps_res_t ps_init(mps_arena_t arena, mps_pool_t pool0, mps_pool_t pool1);
extern void ps_finish(void);

extern mps_res_t ps_scan(mps_ss_t scan_state,
                         mps_addr_t base, mps_addr_t limit);

extern mps_res_t ps_string_init(OBJECT *objOutput, mps_addr_t p, uint16 length);

extern mps_res_t ps_array_init(OBJECT *objOutput, mps_addr_t p, uint16 length,
                               OBJECT *refs, size_t nr_refs);

extern mps_res_t ps_name_init(OBJECT *objOutput, mps_addr_t p, uint16 length,
                              OBJECT *refs, size_t nr_refs);

extern void ps_save(void);
extern void ps_restore(void);
extern void ps_restore_prepare(OBJECT *refs, size_t nr_refs);

extern void object_finalize(mps_addr_t obj);

extern void ps_write_random(OBJECT *obj, OBJECT *refs, size_t nr_refs);

extern int ps_check(OBJECT *obj);


#endif
