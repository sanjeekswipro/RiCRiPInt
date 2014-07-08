/** \file
 * \ingroup core
 *
 * $HopeName: SWv20!src:asyncps.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2011-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Asynchronous PS event handling.
 */

#include "core.h"

#include "mm.h"
#include "control.h"
#include "dicthash.h"
#include "stacks.h"
#include "objects.h"
#include "objstack.h"
#include "swtrace.h"
#include "swerrors.h"  /* FAILURE() */
#include "mlock.h"
#include "swevents.h"
#include "coreinit.h"
#include "eventapi.h"
#include "namedef_.h"
#include "interrupts.h"
#include "params.h"

#include "asyncps.h"


/* An Ullman set is a very simple data structure where the potential set
 * membership is relatively small.  All operations, except iteration, can be
 * performed in constant time.
 */
struct ULLMAN_SET {
  unsigned int  size;
  unsigned int  member[MAX_ASYNC_ID + 1];
  unsigned int  position[MAX_ASYNC_ID + 1];
};

static
Bool is_set_member(
  struct ULLMAN_SET*  set,
  unsigned int  member)
{
  HQASSERT(set != NULL, "is_set_member: NULL set pointer");
  HQASSERT(member <= MAX_ASYNC_ID, "is_set_member: member out of ranger");

  return(set->position[member] < set->size &&
         set->member[set->position[member]] == member);
}

static
void set_add(
  struct ULLMAN_SET*  set,
  unsigned int  member)
{
  if ( !is_set_member(set, member) ) {
    set->position[member] = set->size;
    set->member[set->size] = member;
    set->size++;
  }
}

static
void set_clear(
  struct ULLMAN_SET*  set)
{
  HQASSERT(set != NULL, "set_clear: NULL set pointer");

  set->size = 0;
}

static
unsigned int set_size(
  struct ULLMAN_SET*  set)
{
  HQASSERT(set != NULL, "set_size: NULL set pointer");

  return(set->size);
}

static
unsigned int set_nth(
  struct ULLMAN_SET*  set,
  unsigned int  nth)
{
  HQASSERT(set != NULL, "set_nth: NULL set pointer");
  HQASSERT(nth < set->size, "set_nth: member index too big");

  return(set->member[nth]);
}


static multi_mutex_t  async_ps_mtx;
struct ULLMAN_SET requested_async_ps;

Bool async_ps_pending(void)
{
  /* Updates to set size are always mtx protected.  It is always incremented
   * when actions are queued up and we are only interested in there being any
   * queued.
   */
  return(set_size(&requested_async_ps) > 0);
}


static mm_addr_t async_action_mem = NULL;
static size_t sizeof_async_mem = 0;

static
Bool reclaim_async_memory(void)
{
  if (async_action_mem == NULL) {
    return(FALSE);
  }
  mm_free(mm_pool_temp, async_action_mem, sizeof_async_mem);
  async_action_mem = NULL;
  return(TRUE);
}

static
void alloc_async_memory(SYSTEMPARAMS *systemparams)
{
  if ((sizeof_async_mem = (size_t)systemparams->AsyncMemorySize) > 0) {
    do {
      async_action_mem = mm_alloc(mm_pool_temp, sizeof_async_mem,
                                  MM_ALLOC_CLASS_ASYNC);
      if (async_action_mem != NULL) {
        return;
      }
      sizeof_async_mem >>= 2;
    } while (sizeof_async_mem >= 5000);
  }
}

void init_async_memory(struct SYSTEMPARAMS *systemparams)
{
  (void)reclaim_async_memory();
  alloc_async_memory(systemparams);
}


static
void mark_async_ps(
  OBJECT* async_ps_dict,
  struct ULLMAN_SET*  requested)
{
  OBJECT  thekey = OBJECT_NOTVM_INTEGER(-1);
  OBJECT* theo;
  unsigned int  i;
  unsigned int  size;

  multi_mutex_lock(&async_ps_mtx);
  /* Mark pending async ps procedures in the dict by changing the
   * literal/executable flag on the procedure value.
   * For posterity:
   *    Nasty, but saves having an array which we have to change the size of
   *    when new action procedures come along.
   */
  size = set_size(&requested_async_ps);
  for (i = 0; i < size; i++) {
    object_store_integer(&thekey, set_nth(requested, i));
    theo = extract_hash(async_ps_dict, &thekey);
    if (theo != NULL) {
      theTags(*theo) &= ~EXECUTABLE;
    }
  }
  /* Clear async ps procedures ready for new ones */
  set_clear(&requested_async_ps);
  multi_mutex_unlock(&async_ps_mtx);
}


int32 async_action_level = 0;

static
void do_async_ps(void)
{
  OBJECT* theo;
  corecontext_t* context;
  Bool reclaim_mem = FALSE;
  Bool allow_interrupt_sav;
  Bool outer_between_operators;

  theo = fast_extract_hash_name(&internaldict, NAME_serviceinterrupt);
  if (theo == NULL) {
    HQFAIL("do_async_ps: serviceinterrupt proc not yet defined, please tell core.");
    return;
  }

  context = get_core_context_interp();

  /* We are really short of memory to do an asynchronous action, so free up the
   * bit we sneakily saved away for just such an emergency
   */
  reclaim_mem = reclaim_async_memory();

  async_action_level++;
  outer_between_operators = context->between_operators;
  context->between_operators = FALSE;

  /* Any interrupt or error arising from async postscript may potentially
   * be dropped off when we return from async processing context. So here
   * we only pick up, if any, interrupt requests from async actions but
   * delay processing them to our normal (non-async) run of interpreter.
   */
  allow_interrupt_sav = allow_interrupt;
  allow_interrupt = FALSE;
  if (push(theo, &executionstack)) {
    if (!interpreter(1, NULL))
      error_clear_context(context->error);
  }
  allow_interrupt = allow_interrupt_sav;
  if (allow_interrupt && user_interrupt()) {
    dosomeaction = TRUE;
  }

  context->between_operators = outer_between_operators;
  async_action_level--;
  if (reclaim_mem) {
    alloc_async_memory(context->systemparams);
  }
}


static
Bool unmark_async_ps(
  OBJECT* thekey,
  OBJECT* theo,
  void* argBlockPtr)
{
  UNUSED_PARAM(OBJECT*, thekey);
  UNUSED_PARAM(void*, argBlockPtr);

  theTags(*theo) |= EXECUTABLE;
  return(TRUE);
}

void do_pending_async_ps(void)
{
  OBJECT  thekey = OBJECT_NOTVM_NAME(NAME_execdict, LITERAL);
  OBJECT* thed;
  OBJECT* async_ps_dict;

  /* Don't use nnewobj: it might already be in use */
  if ((thed = fast_sys_extract_hash(&thekey)) == NULL) {
    return;
  }
  if ((async_ps_dict = fast_extract_hash_name(thed, NAME_serviceinterrupt)) == NULL) {
    return;
  }

  /* Mark requested async ps procedures in the dict, invoke the internaldict
   * proc to run them, and then clear the entries in the dict. */
  mark_async_ps(async_ps_dict, &requested_async_ps);
  do_async_ps();
  walk_dictionary(async_ps_dict, unmark_async_ps, NULL);
}

static sw_event_result HQNCALL async_ps_handler(void *context,
                                                sw_event *ev)
{
  SWMSG_ASYNC_PS *msg = ev->message;

  UNUSED_PARAM(void *, context);

  if (msg == NULL || ev->length < sizeof(SWMSG_ASYNC_PS))
    return SW_EVENT_CONTINUE;

  if (msg->id <= MAX_ASYNC_ID) {
    multi_mutex_lock(&async_ps_mtx);
    set_add(&requested_async_ps, msg->id);
    multi_mutex_unlock(&async_ps_mtx);
  }

  return SW_EVENT_CONTINUE;
}

static sw_event_handlers handlers[] = {
  {async_ps_handler, NULL, 0, SWEVT_ASYNC_PS, SW_EVENT_NORMAL }
} ;

static Bool asyncps_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params);

  if ( SwRegisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) != SW_RDR_SUCCESS )
    return FAILURE(FALSE) ;

  set_clear(&requested_async_ps);

  multi_mutex_init(&async_ps_mtx, ASYNCPS_LOCK_INDEX, FALSE,
                   SW_TRACE_ASYNC_PS_ACQUIRE, SW_TRACE_ASYNC_PS_HOLD);

  return TRUE;
}

static void asyncps_finish(void)
{
  multi_mutex_finish(&async_ps_mtx);

  (void)SwDeregisterHandlers(handlers, NUM_ARRAY_ITEMS(handlers)) ;
}

void asyncps_C_globals(core_init_fns *fns)
{
  async_action_mem = NULL;
  sizeof_async_mem = 0;
  async_action_level = 0;

  fns->swstart = asyncps_swstart;
  fns->finish = asyncps_finish;
}


/* Log stripped */
