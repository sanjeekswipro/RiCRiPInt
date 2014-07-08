/* $Id: shared:wcscmmmem.h,v 1.1.10.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This is a lightweight FILELIST wrapper around a 'blob'.  It's required
 * because the WCS XML parser uses xml_parse_stream, which expects a FILELIST.
 * It's private to the WCS CMM code because it doesn't need to worry about PS
 * local/global file lists, so it's not something that's generally useable
 * outside of WCS.  Also it allows data to be delimited without resorting to a
 * SubFileDecode filter.
 */

#ifndef __WCSCMMMEM_H__
#define __WCSCMMMEM_H__

#include "swmemapi.h"

void *wcscmm_alloc(sw_memory_instance *memstate, size_t bytes) ;
void wcscmm_free(sw_memory_instance *memstate, void *mem) ;

#endif

/*
* Log stripped */
