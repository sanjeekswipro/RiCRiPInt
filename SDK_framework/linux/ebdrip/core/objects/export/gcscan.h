/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:gcscan.h(EBDSDK_P.1) $
 * $Id: export:gcscan.h,v 1.23.1.1.1.1 2013/12/19 11:25:00 anon Exp $
 *
 * Copyright (C) 2003-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions of GC scanning functions.
 */

#ifndef __GCSCAN_H__
#define __GCSCAN_H__

#include "mps.h"
#include "objects.h" /* Macros use sizeof(OBJECT) */

struct FILELIST;    /* from COREfileio */
struct GSTATE;      /* from SWv20/COREgstate */
struct PS_SAVEINFO; /* from SWv20 */

extern mps_res_t MPS_CALL ps_scan(mps_ss_t scan_state, mps_addr_t base, mps_addr_t limit);

#define ps_scan_field(scan_state, field_addr) \
  ps_scan(scan_state, (mps_addr_t)(field_addr), \
          (mps_addr_t)((OBJECT *)(field_addr) + 1))


extern mps_res_t MPS_CALL ps_typed_scan(mps_ss_t scan_state,
                               mps_addr_t base, mps_addr_t limit);
extern mps_addr_t MPS_CALL ps_typed_skip( mps_addr_t object );

extern mps_res_t MPS_CALL ps_scan_stack(mps_ss_t ss, STACK *stack);


/* GC scanning utilities */


#define ADDR_ADD(p, s) ((mps_addr_t)((char *)(p) + (s)))
#define ADDR_SUB(p, s) ((mps_addr_t)((char *)(p) - (s)))
#define ADDR_OFFSET(base, limit) ((size_t)((char *)(limit) - (char *)(base)))


/* PS_MARK_BLOCK -- mark a block of PS memory
 *
 * Marks a block of memory from scan_base (inclusive) for scan_len
 * bytes.
 *
 * We know MPS_RETAIN will not change _scan_ptr, because the block will
 * not be moved.  */
#define PS_MARK_BLOCK(scan_state, scan_base, scan_len) \
  MACRO_START \
    mps_addr_t _scan_ptr = (mps_addr_t)(scan_base); \
    mps_addr_t _scan_limit = ADDR_ADD( _scan_ptr, scan_len ); \
    \
    for( ; _scan_ptr < _scan_limit; \
         _scan_ptr = ADDR_ADD( _scan_ptr, sizeof(OBJECT) )) \
      MPS_RETAIN( &_scan_ptr, TRUE ); \
  MACRO_END


/*------------------------- IMPORTED DEFINITIONS ----------------------------*/

/* This routine is imported from COREfileio!src:fileio.c.  It's a
   scanning fn for FILELISTs. */
extern mps_res_t MPS_CALL ps_scan_file(size_t *len_out,
                              mps_ss_t ss, struct FILELIST *fl);
/* This routine is imported from COREfileio!src:fileio.c.  It's a
   finalization fn for FILELISTs. */
extern void fileio_finalize(struct FILELIST *obj);
/* This routine is imported from COREfileio!src:fileio.c.  It's a
   fn to return the size of a FILELIST structure. */
extern size_t filelist_size(struct FILELIST *fl);


/* This routine is imported from SWv20!src:gstate.c.  It's a scanning fn for
   GSTATEs. */
extern mps_res_t MPS_CALL gs_scan(size_t *len_out, mps_ss_t ss, struct GSTATE *gs);
/* This routine is imported from SWv20!src:gstate.c.  It's a
   finalization fn for GSTATEs. */
extern void gstate_finalize(struct GSTATE *obj);
/* This routine is imported from SWv20!src:gstate.c.  It's a fn to
   return the size of a GSTATE structure. */
extern size_t gstate_size(struct GSTATE *gs);


/* This routine is imported from COREobjects!src:ncache.c.  It's a
   finalization fn for NAMECACHEs. */
extern void ncache_finalize(NAMECACHE *obj);

/* This routine is imported from COREobjects!src:ncache.c.  It's a
   scanning fn for a NAMECACHE. */
extern mps_res_t MPS_CALL ncache_scan(size_t *len_out,
                                      mps_ss_t ss, NAMECACHE *nc);

/* This routine is imported from SWv20!src:params.c.  It's a scanning fn for
   MiscHookParams. */
extern mps_res_t MPS_CALL scanMiscHookParams(mps_ss_t ss, void *p, size_t s);

/* This routine is imported from SWv20!src:params.c.  It's a scanning fn for
   UserParams. */
extern mps_res_t MPS_CALL scanUserParams(mps_ss_t ss, void *p, size_t s);

/* This routine is imported from SWpdf!src:pdfin.c.  It's a scanning fn for
   PDFParams. */
extern mps_res_t MPS_CALL pdfparams_scan(mps_ss_t ss, void *p, size_t s);


/* This routine is imported from SWv20!src:psvm.c.  It's a scanning fn for
   PS_SAVEINFOs. */
extern mps_res_t MPS_CALL ps_scan_saveinfo(mps_ss_t ss, struct PS_SAVEINFO *sptr);

/* This routine is imported from COREedoc!src:xpsparams.c.  It's a scanning fn
   for XPSPARAMS. */
extern mps_res_t MPS_CALL xpsparams_scan(mps_ss_t ss, void *p, size_t s);


#endif /* protection for multiple inclusion */

/*
Log stripped */
