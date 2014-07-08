/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!export:swpdfin.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * API to PDF Input functions
 */

#ifndef __SWPDFIN_H__
#define __SWPDFIN_H__

#include "swpdf.h"    /* PDFXCONTEXT */

struct core_init_fns ; /* from SWcore */
struct FILELIST ; /* from COREfileio */
struct GSTATE ; /* from COREgstate */
struct STACK ; /* from COREobjects */
struct OBJECT ; /* from COREobjects */

/** \defgroup pdfin PDF input.
    \ingroup pdf
    \{ */

void pdfin_C_globals(struct core_init_fns *fns) ;

extern PDFXCONTEXT *pdfin_xcontext_base ;

Bool pdf_x_filter_preflight(struct FILELIST *flptr,
                            struct OBJECT *args, struct STACK *stack) ;

Bool pdf_exec_stream(struct OBJECT *stream, int stream_type) ;
Bool pdf_walk_gstack(Bool (*gs_fn)(struct GSTATE *, void *), void *args) ;

Bool pdf_getStrictpdf(PDFXCONTEXT *pdfxc) ;

Bool pdf_newpagedevice(void) ;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
