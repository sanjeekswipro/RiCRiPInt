/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:chartype.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Table of character types which the scanner requires.
 */

#ifndef __CHARTYPE_H__
#define __CHARTYPE_H__

/* --- Exported Functions --- */
extern void remap_bin_token_chars(void);


#define         REGULAR         0x00
#define         WHITE_SPACE     0x01
#define         SPECIAL_CHAR    0x02
#define         BINARY_TOKEN    0x04
#define         END_MARKER_PS   0x08
#define         END_MARKER_PDF  0x10
#define         END_LINE        0x20
#define         COMMS_CHAR      0x40
#define         START_NUM       0x80

extern uint8 _char_table[256];

#if defined( ASSERT_BUILD )


/* Note the test used below is version of (c >= 0 && c < 256) that doesn't cause
warnings on GCC when 'c' is a uint8 (since, in which case, the comparison is
always true, as GCC delights in telling us). */
#define CharTableGet(c) \
  (((c & 0xffffff00) == 0) ? ((int32)_char_table[(c)]) \
                           : (HQFAIL("Invalid access to _char_table"), 0))

#else /* i.e. if (! ASSERT_BUILD) */

#define CharTableGet(c) ((int32)_char_table[(c)])

#endif

#define IsSpecialChar(c)       (CharTableGet(c) & SPECIAL_CHAR)
#define IsWhiteSpace(c)        (CharTableGet(c) & WHITE_SPACE)
#define IsEndMarkerPS(c)       (CharTableGet(c) & END_MARKER_PS)
#define IsEndMarkerPDF(c)      (CharTableGet(c) & END_MARKER_PDF)
#define IsEndOfLine(c)         (CharTableGet(c) & END_LINE)
#define IsStartNum(c)          (CharTableGet(c) & START_NUM)
#define IsBinaryToken(c)       (CharTableGet(c) & BINARY_TOKEN)
#define IsBinarySequence(c)    (CharTableGet(c) & BINARY_SEQUENCE)
#define IsCommunicationChar(c) (CharTableGet(c) & COMMS_CHAR)

#endif /* protection for multiple inclusion */

/* Log stripped */
