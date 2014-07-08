/** \file
 * \ingroup swpfinapi
 *
 * $HopeName: COREinterface_control!swpfinpcl.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2011 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief  This header file provides definitions for PCL functionality
 * supplied by a PFIN (Pluggable Font Interface) module.
 *
 * PFIN miscops are used to provide Font Selection, metrics, downloading and
 * symbolset support.
 */

#ifndef __SWPFINPCL_H__
#define __SWPFINPCL_H__

#include "eventapi.h"  /* EVENT_PCL */

/* -------------------------------------------------------------------------- */
/** \{ */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/** \brief The UFST miscops

\code
  PCL_MISCOP_SPECIFY
  ==================

    'Select' a font by font criteria. The array contains:

    > [0] int    PCL_MISCOP_SPECIFY
      [1] int    symbolset
      [2] int    spacing
      [3] float  pitch  (cpi)
      [4] float  height (pts)
      [5] int    style
      [6] int    weight
      [7] int    typeface
      [8] int    exclude bitmaps

    The reply is always:

    < [0] string  PS fontname          eg "PCL:1234:5678"
      [1] float   fontsize (pts)       note: not "PCL height"
      [2] float   HMI (ems)
      [3] int     symbolset type
      [4] int     spacing (ems)
      [5] float   underline offset (ems)
      [6] float   underline thickness (ems)
      [7] int     ID                   Downloaded PCL5 font ID or -1 if internal
      [8] int     bitmapped


  PCL_MISCOP_SELECT
  =================

    Select a font by font ID.

    This can be used to select:
    a) a PCL5 soft font by numerical ID (array length 5)
    b) any XL font by name, name datatype and boldness (array length 5)
    c) any hard font by location and index (array length 6)

    > [0] int      PCL_MISCOP_SELECT
      [1] int/str  ID        int font ID (a), string name (b), int index (c)
      [2] int      symbolset
      [3] int      boldness  (b)
          float    pitch     (a & c)
      [4] int      datatype  (b)
          float    height    (a & c)
      [5] int      location  (c)

    If the font cannot be selected, there is no reply. Otherwise:

    < [0] string  PS fontname
      [1] float   fontsize
      [2] float   HMI
      [3] int     symbolset           |
      [4] int     spacing             |
      [5] float   pitch               |
      [6] float   height              | criteria as per PCL_MISCOP_SPECIFY
      [7] int     style               |
      [8] int     weight              |
      [9] int     typeface            |
     [10] int     symbolset type
     [11] float   underline offset
     [12] float   underline thickness
     [13] int     ID                  Downloaded PCL5 font ID or -1 if internal
     [14] int     bitmapped


  PCL_MISCOP_METRICS
  ==================

    Get glyph existance, metrics and kerning information.

    > [0] int     PCL_MISCOP_METRICS
      [1] string  PS fontname        or null for same font as last call
      [2] int     character code
      [3] int     wmode              optional: !0 = vert, otherwise horz.

    If the character is undefined there is no reply. Otherwise:

    < [0] float  advance width       or height, if wmode is nonzero


  PCL_MISCOP_XL
  =============

    As PCL_MISCOP_SELECT, but for internal fonts by XL name.


  PCL_MISCOP_FONT
  ===============

    Perform PCL font control actions (<esc>*c<action>F)

    > [0] PCL_MISCOP_FONT
      [1] int      action    from PCL command
      [2] int/str  ID        required for action > 1. String for XL font ID.
      [3] int      code      required for action = 3
          string   font      required for action = 6
      [4] int      datatype  for action > 1 where ID is a string

    The length of the parameter array must therefore be 2 for action<2, and 4 or
    5 otherwise, with the fourth parameter being a string for action=6.

    There is no reply if successful. Otherwise an integer error code is
    returned - see the PCL_ERROR enumeration below.


  PCL_MISCOP_SYMBOLSET
  ====================

    Perform PCL symbolset control actions (<esc>*c<action>S)

    > [0] PCL_MISCOP_SYMBOLSET
      [1] int     action  from PCL command
      [2] int     ID      required for action > 1

    The length of the parameter array must therefore be 2 for action<2, and 3
    otherwise.

    There is no reply.


  PCL_MISCOP_DEFINE_FONT
  ======================

    Define a font header with data from PCL command <esc>)s<length>W<data>

    > [0] PCL_MISCOP_DEFINE_FONT
      [1] int/str  ID        from <esc>*c<id>D Font ID command, or XL name
      [2] string   data      directly from PCL data
      [3] int      datatype  if ID is string

    The length of the parameter array is therefore 3 for PCL5, and 4 for XL.

    There is no reply if successful. Otherwise an integer error code is
    returned - see the PCL_ERROR enumeration below.


  PCL_MISCOP_DEFINE_GLYPH
  =======================

    Define a font glyph with data from PCL command <esc>(s<length>W<data>

    > [0] PCL_MISCOP_DEFINE_GLYPH
      [1] int/str ID        from previous <esc>*c<id>D Font ID, or XL name
      [2] int     code      from previous <esc>*c<code>E character code command
      [3] string  data      directly from PCL data
      [4] int     datatype  if ID is string

    The length of the parameter array is therefore 4 for PCL5, and 5 for XL.

    There is no reply if successful. Otherwise an integer error code is
    returned - see the PCL_ERROR enumeration below.


  PCL_MISCOP_DEFINE_SYMBOLSET
  ===========================

    Define a symbol set with data from PCL command <esc>(f<length>W<data>

    > [0] PCL_MISCOP_DEFINE_SYMBOLSET
      [1] int     ID    from previous <esc>*c<ss>R symbol set command
      [2] string  data  directly from PCL data

    There is no reply.
\endcode
*/

enum {
  PCL_MISCOP_SPECIFY = 0,      /* Select font by PCL 5 criteria */
  PCL_MISCOP_SELECT,           /* Select font by PCL 5 ID */
  PCL_MISCOP_METRICS,          /* Return character information */
  PCL_MISCOP_XL,               /* Select font by PCL XL name */
  PCL_MISCOP_FONT,             /* Perform PCL font control actions */
  PCL_MISCOP_SYMBOLSET,        /* Perform PCL symbol set control actions */
  PCL_MISCOP_DEFINE_FONT,      /* Define a font header */
  PCL_MISCOP_DEFINE_GLYPH,     /* Define a font glyph */
  PCL_MISCOP_DEFINE_SYMBOLSET  /* Define a symbol set */
} ;


/** \brief  DataFormat (namespace) to be used for PCL5 alphanumeric IDs.

    This is to avoid matching any XL-defined font, and also to discern PCL5
    alphanumeric ID'd TrueTypes from XL's faked TrueType headers.
*/

#define PCL_ALPHANUMERIC 1024


/** \brief  Events generated by the PCL font system */

enum {
  EVENT_PCL_FONT_DELETED = EVENT_PCL+1,
  EVENT_PCL_SS_DELETED,
  EVENT_PCL_SS_DEFINED
} ;

/* ========================================================================== */
/* Error codes defined by the PCL XL spec. */

enum {
  PCL_ERROR_INSUFFICIENT_MEMORY = 1003,         /* Unable to define font/glyph */
  PCL_ERROR_CANNOT_REPLACE_CHARACTER = 1046,    /* Glyph definition for non-user-font */
  PCL_ERROR_FONT_UNDEFINED,                     /* Glyph definition for unknown font */
  PCL_ERROR_FONT_NAME_ALREADY_EXISTS,           /* XL fonts cannot be redefined */
  PCL_ERROR_FST_MISMATCH,                       /* Glyph format doesn't match font */
  PCL_ERROR_UNSUPPORTED_CHARACTER_CLASS,        /* Bad character class */
  PCL_ERROR_UNSUPPORTED_CHARACTER_FORMAT,       /* Bad character format */
  PCL_ERROR_ILLEGAL_CHARACTER_DATA,
  PCL_ERROR_ILLEGAL_FONT_DATA,
  PCL_ERROR_ILLEGAL_FONT_HEADER_FIELDS,         /* Bad font format/FST/variety/orientation */
  PCL_ERROR_ILLEGAL_NULL_SEGMENT_SIZE,          /* Null segment wrong size */
  PCL_ERROR_ILLEGAL_FONT_SEGMENT,               /* Unexpected segment */
  PCL_ERROR_MISSING_REQUIRED_SEGMENT,           /* Missing segment */
  PCL_ERROR_ILLEGAL_GLOBAL_TRUE_TYPE_SEGMENT,   /* GTTS unrecognised format */
  PCL_ERROR_ILLEGAL_GALLEY_CHARACTER_SEGMENT,   /* Galley unrecognised format */
  PCL_ERROR_ILLEGAL_VERTICAL_TX_SEGMENT,        /* unrecognised format */
  PCL_ERROR_ILLEGAL_BITMAP_RESOLUTION_SEGMENT,  /* unrecognised format */
  PCL_ERROR_UNDEFINED_FONT_NOT_REMOVED,         /* Can't delete undefined font */
  PCL_ERROR_INTERNAL_FONT_NOT_REMOVED,          /* Can't delete internal font */
  PCL_ERROR_MASS_STORAGE_FONT_NOT_REMOVED,      /* Can't delete disk font */
  PCL_ERROR_NO_CURRENT_FONT,
  PCL_ERROR_BAD_FONT_DATA,
  PCL_ERROR_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND, /* No such font and nothing like it */
  PCL_ERROR_FONT_SUBSTITUTED_BY_FONT,           /* Similar font found */
  PCL_ERROR_SYMBOL_SET_REMAP_UNDEFINED          /* Symbolset mismatch */
} ;

/* ========================================================================== */
#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWPFINPCL_H__ */
