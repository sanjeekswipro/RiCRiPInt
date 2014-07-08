/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:v20start.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript interpreter initialisation.
 */

#ifndef __V20START_H__
#define __V20START_H__

struct core_init_fns ;

void start_interpreter(void);
void stop_interpreter(void);

void v20_C_globals(struct core_init_fns *fns) ;
void pagebuffer_C_globals(struct core_init_fns *fns) ;
void v20render_C_globals(struct core_init_fns* fns);


/*
Log stripped */
#endif /* protection for multiple inclusion */
