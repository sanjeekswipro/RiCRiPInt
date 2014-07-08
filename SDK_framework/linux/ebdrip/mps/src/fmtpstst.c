/* impl.c.fmtpstst: POSTSCRIPT OBJECT FORMAT TEST VERSION
 *
 *  $Id: fmtpstst.c,v 1.24.4.2.1.1 2013/12/19 11:27:05 anon Exp $
 *  $HopeName: MMsrc!fmtpstst.c(EBDSDK_P.1) $
 *  Copyright (c) 2001 Ravenbrook Limited.
 *  Copyright (C) 2008-2013 Global Graphics Software Ltd. All rights reserved.
 *  Global Graphics Software Ltd. Confidential Information.
 *
 * .readership: SW developers
 *
 * .purpose: This is a simplified version of the PS object format for
 * internal MM tests.  Cf. SWmm_common!src:mmps.c and
 * COREobjects!src:objects.c.
 *
 * .note: This file, being in the MPS module, does not depend on SW core
 * in any way; it is a partial copy.  So it doesn't have to track
 * changes in the core, unless they affect MM behaviour and should be
 * tested for that aspect.  Conversely, if you do change it, you must
 * copy all core changes, none will be included automatically! */

#include "testlib.h"
#include "fmtpscon.h"
#include "fmtpstst.h"
#include "mps.h"
#include "mpscepvm.h"
#include "mpscmvff.h"
#include "misc.h"
#include <stdlib.h>


#define ALIGNMENT sizeof(OBJECT)


#define NOTREACHED              cdie(0, "NOTREACHED " STR(__LINE__))
#define HQFAIL(msg)             cdie(0, msg)
#define HQASSERT(cond, msg)     cdie(cond, msg)
#define UNUSED_PARAM(type, v)   testlib_unused(v)

#define DWORD_ALIGN_UP(x)       (((mps_word_t)(x)+7)&~7)
#define DWORD_ALIGN_DOWN(x)     ((mps_word_t)(x)&~7)
#define DWORD_IS_ALIGNED(x)     (((mps_word_t)(x)&7)==0)

#define ADDR_ADD(p, s)          ((mps_addr_t)((char *)(p) + (s)))


OBJECT onull = OBJECT_NOTVM_NULL; /* Null object */


/* pools */

static mps_pool_t pool[2];


/* save levels */

corecontext_t Context = { 0, ISLOCAL };

#define get_core_context() (&Context)

typedef struct saveRecord {
  OBJECT oldvalue;
  OBJECT *slot;
  struct saveRecord *next;
} saveRecord;

static saveRecord *saves[MAXSAVELEVELS];

static NAMECACHE *namepurges;

static mps_pool_t savePool;
static mps_root_t saveRoot;


/* ps_write -- write a field in an array and store restore info */

static void ps_write(int8 level, OBJECT *dest, OBJECT *src)
{
  corecontext_t *corecontext = get_core_context() ;

  /* Save it if the array will survive the object being written into it. */
  if (level < (theMark(*src) & SAVEMASK)) {
    void *p;
    saveRecord *sl;
    size_t saveIndex = NUMBERSAVES(corecontext->savelevel);

    die(mps_alloc(&p, savePool, sizeof(saveRecord)), "save alloc");
    sl = (saveRecord *)p;
    sl->slot = dest;
    Copy( &sl->oldvalue, dest );
    sl->next = saves[saveIndex]; saves[saveIndex] = sl;
    SETSLOTSAVED(*dest, FALSE, corecontext);
  }
  Copy(dest, src);
}


/* ps_save - simulate a PS save */

void ps_save(void)
{
  mps_epvm_save(pool[0]);
  mps_epvm_save(pool[1]);
  Context.savelevel += SAVELEVELINC;
}


/* ps_restore - simulate a PS restore */

void ps_restore(void)
{
  saveRecord *sr;
  saveRecord *sr_next;
  size_t saveIndex = NUMBERSAVES(Context.savelevel);

  /* Restore the old values from the saveRecords */
  for (sr = saves[saveIndex]; sr != NULL; sr = sr_next) {
    Copy(sr->slot, &sr->oldvalue);
    sr_next = sr->next;
    mps_free(savePool, sr, sizeof(saveRecord));
  }
  saves[saveIndex] = NULL;

  /* Drop names at this level from the namepurges chain. */
  while (namepurges != NULL && namepurges->sid == Context.savelevel)
    namepurges = namepurges->dictsid;

  Context.savelevel -= SAVELEVELINC;
  mps_epvm_restore(pool[1], saveIndex-1);
  mps_epvm_restore(pool[0], saveIndex-1);
}


/* ps_restore_prepare - clear all refs to current save level */

void ps_restore_prepare(OBJECT *refs, size_t nr_refs)
{
  size_t r;

  for (r = 0; r < nr_refs; ++r) {
    if ((theMark(refs[r]) & SAVEMASK) == Context.savelevel)
      theTags(refs[r]) = ONULL | LITERAL;
  }
}


/* scan_saves - scan the save records */

static mps_res_t MPS_CALL scan_saves(mps_ss_t ss, void *p, size_t s)
{
  size_t i;
  saveRecord *sr;

  testlib_unused(p); testlib_unused(s);
  MPS_SCAN_BEGIN(ss)
    for (i = 0; i < MAXSAVELEVELS; ++i)
      for (sr = saves[i]; sr != NULL; sr = sr->next) {
        MPS_RETAIN((mps_addr_t*)&sr->slot, TRUE);
        MPS_SCAN_CALL((void)ps_scan(ss, &sr->oldvalue, &sr->oldvalue + 1));
      }
  MPS_SCAN_END(ss);
  return MPS_RES_OK;
}


/* namepurges_unlink - unlink the name from the purge list if it's on it. */

static int32 namepurges_unlink(NAMECACHE **namepurges_loc, NAMECACHE *obj)
{
  register NAMECACHE *curr;
  register NAMECACHE **prev;

  /* Run down the purge list and unlink the name */
  prev = namepurges_loc;
  while (( curr = *prev ) != NULL ) {
    if ( curr == obj ) {
      *prev = curr->dictsid;
      return TRUE;
    } else {
      prev = &curr->dictsid;
    }
  }
  return FALSE;
}


/* ps_init -- initialize the fmtps module */

mps_res_t ps_init(mps_arena_t arena, mps_pool_t pool0, mps_pool_t pool1)
{
  size_t i;
  mps_res_t res;

  pool[0] = pool0; pool[1] = pool1;
  for (i = 0; i < MAXSAVELEVELS; ++i)
    saves[i] = NULL;
  res = mps_root_create(&saveRoot, arena, mps_rank_exact(), 0,
                        scan_saves, NULL, 0);
  if (res != MPS_RES_OK) return res;
  res = mps_pool_create(&savePool, arena, mps_class_mvff(), 8192,
                        sizeof(saveRecord), (mps_align_t)sizeof(OBJECT),
                        TRUE, TRUE, TRUE);
  if (res != MPS_RES_OK) goto failPool;

  Context.savelevel = 0; ps_save(); /* ps_*_init assume global savelevel */
  namepurges = NULL;
  return MPS_RES_OK;

failPool:
  mps_root_destroy(saveRoot);
  return res;
}


/* ps_finish - finish the fmtps module */

void ps_finish(void)
{
  mps_pool_destroy(savePool);
  mps_root_destroy(saveRoot);
}


/* ps_scan -- a scan method for the format */

mps_res_t MPS_CALL ps_scan(mps_ss_t scan_state, mps_addr_t base, mps_addr_t limit)
{
  register OBJECT *obj;
  OBJECT *obj_limit;
  register mps_addr_t ref;
  size_t len = 0;

  obj_limit = limit;
  MPS_SCAN_BEGIN( scan_state )
    for ( obj = base; obj < obj_limit; obj++ ) {
      ref = (mps_addr_t)oOther( *obj );
      switch ( oType( *obj )) {
      case ONAME:
        MPS_RETAIN( (mps_addr_t *)&oName( *obj ), TRUE );
        continue;
      case OSAVE:
        continue;
      case ODICTIONARY:
        NOTREACHED;
        break;
      case OSTRING: {
        mps_addr_t ref_limit;

        ref_limit = ADDR_ADD( ref, theLen(*obj));
        /* ref could point into the middle of a string, so align it. */
        ref = PTR_ALIGN_DOWN( mps_addr_t, ref, MM_PS_ALIGNMENT );
        len = ADDR_OFFSET( ref, ref_limit );
      } break;
      case OFILE:
        NOTREACHED;
        break;
      case OARRAY:
      case OPACKEDARRAY:
        len = theLen(*obj) * sizeof( OBJECT );
        break;
      case OGSTATE:
      case OLONGSTRING:
        NOTREACHED;
        break;
      default: continue; /* not a composite object */
      }
      PS_MARK_BLOCK( scan_state, ref, len );
    }
  MPS_SCAN_END(scan_state);
  return MPS_RES_OK;
}


/* ps_typed_scan - scan method for a typed pool */

static mps_res_t MPS_CALL ps_typed_scan(mps_ss_t scan_state,
                                        mps_addr_t base, mps_addr_t limit)
{
  TypeTag tag;
  mps_addr_t obj;
  size_t size;
  mps_res_t res = MPS_RES_OK;

  MPS_SCAN_BEGIN(scan_state)
    obj = base;
    while ( obj < limit ) {
      /* The tag is in the same place in all types, that's the point of it. */
      tag = ( (struct generic_typed_object *)obj )->typetag;
      switch (tag) {
      case tag_NCACHE: {
        NAMECACHE *nc = (NAMECACHE *)obj;

        /* The next, dictobj and dictcpy fields are not fixed because we
         * don't use them in the test.  The dictsid field is not fixed,
         * because it's a weak reference cleared by finalization.
         *
         * The length calculation is relying on the name being allocated
         * as a part of the NAMECACHE object. */
        if (!MPS_IS_RETAINED((mps_addr_t*)&nc->dictval, TRUE))
          nc->dictval = NULL;
        size = sizeof(NAMECACHE) + nc->len;
      } break;
      default: {
        HQFAIL("Invalid tag in scan");
        res = MPS_RES_FAIL;
        size = 4; /* No value correct here; this to silence the compiler. */
        break;
      }
      }
      obj = ADDR_ADD( obj, SIZE_ALIGN_UP( size, MM_PS_TYPED_ALIGNMENT ));
    }
  MPS_SCAN_END(scan_state);
  return res;
}


/* ps_typed_skip -- skip method for a typed pool */

static mps_addr_t MPS_CALL ps_typed_skip( mps_addr_t object )
{
  TypeTag tag;
  size_t size;

  tag = ( (struct generic_typed_object *)object )->typetag;
  HQASSERT( DWORD_IS_ALIGNED( object ), "unaligned object" );
  switch (tag) {
  case tag_NCACHE: {
    size = sizeof(NAMECACHE) + ((NAMECACHE *)object)->len;
  } break;
  default:
    HQFAIL("Invalid tag in skip");
    size = 4; /* No value correct here; this to silence the compiler. */
  }
  return ADDR_ADD( object, SIZE_ALIGN_UP( size, MM_PS_TYPED_ALIGNMENT ));
}


/* ps_no_copy -- a dummy copy method for the format */
static void MPS_CALL ps_no_copy(mps_addr_t old, mps_addr_t new)
{
  UNUSED_PARAM(mps_addr_t, old); UNUSED_PARAM(mps_addr_t, new);
  NOTREACHED;
}


/* ps_no_fwd -- a dummy forwarding method for the format */
static void MPS_CALL ps_no_fwd(mps_addr_t old, mps_addr_t new)
{
  UNUSED_PARAM(mps_addr_t, old); UNUSED_PARAM(mps_addr_t, new);
  NOTREACHED;
}


/* ps_no_isfwd -- a dummy isfwd method for the format */
static mps_addr_t MPS_CALL ps_no_isfwd(mps_addr_t object)
{
  UNUSED_PARAM(mps_addr_t, object);
  NOTREACHED; return NULL;
}


/* ps_no_pad -- a dummy pad method for the format */
static void MPS_CALL ps_no_pad(mps_addr_t addr, size_t size)
{
  UNUSED_PARAM(mps_addr_t, addr); UNUSED_PARAM(size_t, size);
  NOTREACHED;
}


/* ps_fixed_format -- the actual format object for PS VM */
static struct mps_fmt_fixed_s ps_fixed_format = {
  ( mps_align_t )sizeof(OBJECT),
  ps_scan, ps_no_fwd, ps_no_isfwd, ps_no_pad
} ;


/* ps_fmt_fixed -- return the fixed format object */

mps_fmt_fixed_s *ps_fmt_fixed(void)
{
  return &ps_fixed_format;
}


/* ps_typed_format -- the actual format object for typed structs */

static struct mps_fmt_A_s ps_typed_format = {
  ( mps_align_t )MM_PS_TYPED_ALIGNMENT,
  ps_typed_scan, ps_typed_skip,
  ps_no_copy, ps_no_fwd, ps_no_isfwd, ps_no_pad
} ;


/* ps_fmt_typed -- return the format object */

mps_fmt_A_s *ps_fmt_typed(void)
{
  return &ps_typed_format;
}


/* object_finalize -- the callback for finalizing an object */

void object_finalize(mps_addr_t obj)
{
  TypeTag tag;

  tag = ( (struct generic_typed_object *)obj )->typetag;
  switch (tag) {
  case tag_NCACHE: {
    (void)namepurges_unlink( &namepurges, (NAMECACHE *)obj );
  } break;
  default: HQFAIL("Invalid tag in finalize");
  }
}


/* INITIALIZATION FUNCTIONS */


/* ps_string_init -- initialize a string to content ps_check_obj can check */

mps_res_t ps_string_init(OBJECT *objOutput, mps_addr_t p, uint16 length){

  uint8 *cp = (uint8 *)p;
  uint16 n;
  corecontext_t *corecontext = get_core_context() ;

  for (n = length; n > 0; --n, ++cp)
    *cp = (uint8)((mps_word_t)cp % 256);

  theTags(*objOutput) = OSTRING | LITERAL | UNLIMITED;
  theMark(*objOutput) = (int8)(ISLOCAL|GLMODE_SAVELEVEL(FALSE, corecontext));
  theLen(*objOutput) = length;
  oString(*objOutput) = p;
  return MPS_RES_OK;
}


/* ps_array_init -- initialize an array to random content from refs */

mps_res_t ps_array_init(OBJECT *objOutput, mps_addr_t p, uint16 length,
                        OBJECT *refs, size_t nr_refs)
{
  OBJECT *arr = (OBJECT *)p;
  OBJECT *o;
  OBJECT transfer;
  size_t r;
  corecontext_t *corecontext = get_core_context() ;

  /* Set the slot flags for all of the objects just allocated to indicate
     this is a PSVM object, saved at the current savelevel, and the
     object in the slot is local. Since we're touching the slot contents,
     we might as well initialise the object tags to ONULL. */
  theMark(transfer) = (int8)(ISPSVM|ISLOCAL|GLMODE_SAVELEVEL(FALSE, corecontext));
  theTags(transfer) = ONULL | LITERAL;
  theLen(transfer) = 0;

  for ( o = (OBJECT *)p, r = length ; r > 0 ; ++o, --r ) {
    OBJECT_SET_D0(*o, OBJECT_GET_D0(transfer)) ; /* Set slot properties */
    OBJECT_SCRIBBLE_D1(*o) ;
  }

  if (refs != NULL)
    for (r = 0; r < length; ++r) {
      size_t rr = (size_t)rnd() % nr_refs;
      uint16 oldlen, offset, newlen;

      OCopy(arr[r], refs[rr]);
      if (oType(arr[r]) == OARRAY || oType(arr[r]) == OSTRING) {
        /* truncate it to a substring or subarray */
        oldlen = theLen(arr[r]);
        offset = (uint16)((uint16)rnd() % (oldlen + 1));
        newlen = (uint16)((uint16)rnd() % (oldlen - offset + 1));
        theLen(arr[r]) = newlen;
        if (oType(arr[r]) == OSTRING)
          oString(arr[r]) += offset;
        else
          oArray(arr[r]) += offset;
      }
    }

  theTags(*objOutput) = OARRAY | LITERAL | UNLIMITED;
  theMark(*objOutput) = (int8)(ISLOCAL|GLMODE_SAVELEVEL(FALSE, corecontext));
  theLen(*objOutput) = length;
  oArray(*objOutput) = arr;
  return MPS_RES_OK;
}


/* ps_name_init -- initialize a name to random content from refs */

mps_res_t ps_name_init(OBJECT *objOutput, mps_addr_t p, uint16 length,
                       OBJECT *refs, size_t nr_refs)
{
  NAMECACHE *nc = (NAMECACHE *)p;
  size_t n;
  uint8 *cp;
  corecontext_t *corecontext = get_core_context() ;

  nc->typetag = tag_NCACHE; nc->sid = corecontext->savelevel;
  if (refs == NULL) {
    nc->dictval = NULL;
  } else {
    /* Picks a random element in refs; it it's an array, dictval is set
     * to a random element of the array. */
    size_t rr = (size_t)rnd() % nr_refs;
    OBJECT *obj = &refs[rr];

    nc->dictval = NULL;
    if (oType(*obj) == OARRAY) {
      size_t t = theLen(*obj);

      if (t > 0) {
        size_t i = rnd() % t;
        nc->dictval = &(oArray(*obj)[i]);
      }
    }
    /* link it into the namepurges chain */
    nc->dictsid = namepurges; namepurges = nc;
  }
  /* init name with predictable junk, ps_check_obj can check it */
  cp = nc->clist = (uint8 *)(nc + 1);
  nc->len = (uint8)length;
  for (n = length; n > 0; --n, ++cp)
    *cp = (uint8)(n % 256);

  theTags(*objOutput) = ONAME | LITERAL | UNLIMITED;
  theMark(*objOutput) = (int8)(ISLOCAL|GLMODE_SAVELEVEL(FALSE, corecontext));
  theLen(*objOutput) = length;
  oName(*objOutput) = nc;
  return MPS_RES_OK;
}


/* ps_write_random -- modify the object to point to a random element of refs */

void ps_write_random(OBJECT *obj, OBJECT *refs, size_t nr_refs)
{
  if (oType(*obj) == OARRAY) {
    size_t t = theLen(*obj);

    if (t > 0) {
      size_t i = rnd() % t;
      size_t rr = (size_t)rnd() % nr_refs;
      OBJECT *body = oArray(*obj);

      ps_write(theMark(*obj) & SAVEMASK, &body[i], &refs[rr]);
    }
  }
}


/* CHECKING FUNCTIONS */

/* ps_check_obj - check an object and its children
 *
 * Doesn't check if the recursion level has reached zero. */

static void ps_check_obj(OBJECT *obj, int level)
{
  size_t r;

  cdie(DWORD_IS_ALIGNED(obj), "ps_check_obj unaligned");
  if (level == 0) return;
  --level;

  switch (oType(*obj)) {
  case ONULL: break;
  case ONAME: {
    NAMECACHE *nc = oName(*obj);
    uint8 *cp = nc->clist;
    size_t n;

    if ( nc->dictval != NULL )
      ps_check_obj(nc->dictval, level);
    for (n = nc->len; n > 0; --n, ++cp)
      cdie(*cp == (uint8)(n % 256), "ps_check_obj");
  } break;
  case OSAVE:
  case ODICTIONARY:
    NOTREACHED;
    break;
  case OSTRING: {
    uint8 *cp = oString(*obj);
    size_t n;

    for (n = theLen(*obj); n > 0; --n, ++cp)
      cdie(*cp == (uint8)((mps_word_t)cp % 256), "ps_check_obj");
  } break;
  case OFILE:
  case OGSTATE:
    NOTREACHED;
    break;
  case OARRAY:
  case OPACKEDARRAY: {
    OBJECT *cont = oArray(*obj);

    for (r = 0; r < theLen(*obj); ++r)
      ps_check_obj(&cont[r], level);
  } break;
  default: NOTREACHED;
  }
}


int ps_check(OBJECT *obj)
{
  ps_check_obj(obj, 4);
  return 1;
}
