/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsfonts.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps font cache interface.
 */

#ifndef __XPSFONTS_H__
#define __XPSFONTS_H__ (1)

#include "xmlgtype.h"   /* xmlGFilter */
#include "objecth.h"    /* OBJECT */
#include "xpsparts.h"

struct SWSTART ; /* from COREinterface */

Bool xps_font_cache_swstart(struct SWSTART *params) ;
void xps_font_cache_finish(void) ;

/**
 * \brief
 * Purge the xps font cache.
 */
extern
void xps_font_cache_purge(void);

/**
 * \brief
 * Load and define a font from an xps package.
 *
 * The font URI is first made an absolute URI if it is in relative form. Next
 * the type of the font referenced is checked in the xps content type stream.
 * If it is a recognised font type then an attempt is made to load the font into
 * the RIP, and the it's font dictionary returned if successful.
 *
 * A cache is kept of all fonts referenced in an xps, indexed on the absolute
 * font URI.  A check of the cache is done after the absolute font URI is
 * resolved, and new fonts are added if they have been successfully loaded.
 *
 * \param[in] font_partname
 * Part name of font to load and define.
 * \param[in] subfont_index
 * 0-based index of required font in truetype collection.
 * \param[in] filter
 * Pointer to XML filter.
 * \param[out] p_font_dict
 * PS dictionary object for font if it can be loaded and defined.
 * \param[in] wmode
 * Non-zero if 'sideways'.
 *
 * \return
 * \c TRUE if font is loaded and defined, else \c FALSE.
 */
extern
Bool xps_font_define(
/*@in@*/ /*@notnull@*/
  xps_partname_t* font_partname,
/*@in@*/
  int32         subfont_index,
/*@in@*/ /*@notnull@*/
  xmlGFilter*   filter,
/*@in@*/ /*@notnull@*/
  OBJECT**      p_font_dict,
  int32         wmode);

#endif

/* Log stripped */
