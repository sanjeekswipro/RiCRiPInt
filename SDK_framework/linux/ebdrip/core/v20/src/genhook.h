/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:genhook.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Generic hook handling code.
 */

#ifndef GENHOOK_H
#define GENHOOK_H

typedef enum {
  GENHOOK_StartImage,
  GENHOOK_EndImage,
  GENHOOK_StartVignette,
  GENHOOK_EndVignette,
  GENHOOK_StartPainting,
  GENHOOK_StartPartialRender,
  GENHOOK_StartRender,
  GENHOOK_EndRender,
  GENHOOK_StartJob,
  GENHOOK_EndJob,
  MAX_HOOK
} GenHookId;

Bool runHooks(OBJECT *dict, GenHookId id);

/* If fStatus is TRUE then the next call to do_run_hooks will attempt to find the hook
   and run it. If it finds it it will repeat the operation the next time, but if it
   doesn't, it will automatically set the status to FALSE and not bother checking
   again.

   On the other hand, if fStatus is set to FALSE that hook is disabled until notified
   to the contrary. That means it can be run once and then disabled (as for
   StartPainting).

   genHook == MAX_HOOK means apply setting to all hooks.
 */

void setHookStatus(GenHookId genhookId, Bool fStatus);

#endif /* GENHOOK_H */

/* Log stripped */
