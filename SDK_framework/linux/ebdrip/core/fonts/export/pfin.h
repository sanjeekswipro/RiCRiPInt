/** \file
 * \ingroup PFIN
 *
 * $HopeName: COREfonts!export:pfin.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PFIN's interface to the font code.
 */

#ifndef __PFIN_H__
#define __PFIN_H__

#include "swmemapi.h" /* sw_memory_instance */
#include "mm.h"       /* mm_pool_t */
#include "fontt.h"    /* charcontext_t */

struct OBJECT ; /* from SWv20 */
struct LINELIST ; /* from SWv20 */
struct sw_pfin ; /* from swpfinapi.h */
struct core_init_fns ; /* from SWcore */

/* -------------------------------------------------------------------------- */

extern font_methods_t font_pfin_fns ;

void pfin_C_globals(struct core_init_fns *fns) ;

Bool pfin_cache(corecontext_t *context,
                charcontext_t *charcontext, struct LINELIST *currpt, int32 gid) ;
Bool pfin_offer_font(int32 fonttype, struct OBJECT* dict) ;

Bool pfin_austerity(struct sw_pfin *ignore) ;

/* -------------------------------------------------------------------------- */
/* FID length field used for PFIN ownership:
   b15   = owned by PFIN
   b14-0 = cycle number, or 0
*/
enum {
  PFIN_FLAGS = 0xFFFF,
  PFIN_OWNED = 0x8000,
  PFIN_MASK  = 0x7FFF
} ;

extern int32 pfin_cycle ;

/* -------------------------------------------------------------------------- */

#endif

/* Log stripped */
