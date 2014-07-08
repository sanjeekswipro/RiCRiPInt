/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:fontdata.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for the unified font data cache interface. This
 * interface provides a mechanism to access and cache data used for
 * charstring definitions. The font data cache provides safe caching and
 * restoring of data.
 */

#ifndef __FONTDATA_H__
#define __FONTDATA_H__

#include "fontt.h"
#include "blobdata.h"
#include "mps.h"

struct OBJECT ; /* from COREobjects */
struct core_init_fns ;

typedef struct fontdata_t fontdata_t ;

void fontdata_C_globals(struct core_init_fns *fns) ;

/** Open the use of a fontdata object. If fontdata_open succeeds,
    fontdata_close MUST be called. The fontdata methods pointer MUST be a
    pointer to static or global data; the methods may get called even after
    fontdata_close has been called. The fontdata cache may hold onto some
    data even over restores, if it can identify the data by a global
    reference. The pointer returned by fontdata_open is only valid between
    matched open and close methods, even if the data is retained. */
fontdata_t *fontdata_open(struct OBJECT *object,
                          const blobdata_methods_t *methods) ;

/** Close the use of a fontdata object. If fontdata_open succeeds,
    fontdata_close MUST be called. */
void fontdata_close(fontdata_t **font_data) ;

/** This is the main routine used by consumers of font data. It provides
    access to a contiguous buffer ("frame") of data, starting at the specified
    offset and having a specified length. The frame of data persists until the
    fontdata close routine is called. */
uint8 *fontdata_frame(fontdata_t *font_data, uint32 offset, uint32 length,
                      size_t alignment) ;

/** This routine determines what type of protection the fontdata uses. It will
    usually be used to determine if a font is Hqx-encrypted, but could be used
    with other font filters. */
uint8 fontdata_protection(struct OBJECT *object,
                          const blobdata_methods_t *methods) ;

/* These sets of pre-defined methods exist, for the common cases of loading
   data from a single string, a file, or a TrueType sfnts array. More
   complicated methods may be provided by the caller. */
extern const blobdata_methods_t blobdata_sfnts_methods ;  /* fdsfnts.c */

extern blobdata_cache_t *font_data_cache ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
