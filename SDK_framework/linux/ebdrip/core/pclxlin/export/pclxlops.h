/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!export:pclxlops.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * pclxlexec initialise/terminate interface.
 */

#ifndef __PCLXLOPS_H__
#define __PCLXLOPS_H__ (1)

struct core_init_fns ; /* from SWcore */

/**
 * \defgroup pclxl PCLXL-Core RIP interface.
 * \ingroup pcl
 * \{ */

void pclxl_C_globals(struct core_init_fns *fns);

/*
 * Note that pclxlexec() is not published as part of
 * this PCLXL "in" interface because pclxlexec is registered
 * as a PostScript operator.
 *
 * This means that, in the first instance, to push a PCLXL data stream
 * through the RIP it must be prefixed by a small fragment of PostScript
 * which basically invokes the pclxlexec operator to process the remainder
 * of the input stream until end-of-file is reached
 */

/** \} */

/* ============================================================================
* Log stripped */
#endif
