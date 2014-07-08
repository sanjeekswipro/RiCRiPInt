/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5scan.h(EBDSDK_P.1) $
 * $Id: src:pcl5scan.h,v 1.23.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scanners for PCL5.
 */

#ifndef __PCL5SCAN_H__
#define __PCL5SCAN_H__

#include "fileio.h"
#include "pcl5context.h"

/* Use Boolean values for backward compatibility. */
#define NOSIGN FALSE
#define EXPLICIT_POSITIVE TRUE
#define EXPLICIT_NEGATIVE -1

/**
 * Evaluates to true if the specified value lies within the specified inclusive
 * range.
 */
#define IN_RANGE(_v, _min, _max) ((_v) >= (_min) && (_v) <= (_max))

extern
PCL5Real pcl5_round_to_ndp(
  PCL5Real  value,
  uint32    digits);

/* Round to 1 decimal place. */
#define pcl5_round_to_1d(v)     pcl5_round_to_ndp((v), 1)

/* Round value to 2 decimal places. */
#define pcl5_round_to_2d(v)     pcl5_round_to_ndp((v), 2)

/* Round value to 4 decimal places. */
#define pcl5_round_to_4d(v)     pcl5_round_to_ndp((v), 4)

extern
PCL5Real pcl5_truncate_to_ndp(
  PCL5Real  value,
  uint32    digits);

/* Truncate value to 4 decimal places. */
#define pcl5_truncate_to_4d(v)  pcl5_truncate_to_ndp((v), 4)

/* Limit the value to the specified range. */
PCL5Real pcl5_limit_to_range(PCL5Real value, PCL5Real min, PCL5Real max) ;

#define ESCAPE 27

/*
From PCL5 Technical Reference. Two-character escape sequences have the
following form:

  Ec X

where X is a character that defines the operation to be performed. X
may be any character from the ASCII table within the range 48-126
decimal (0 through ~ - see Appendix A).


From PCL5 Technical Reference. Parameterized escape sequences have the
following form:

X y z1 # z2 # z3 ... # Zn[data]

where y, #, zi (z1, z2, z3...) and [data] may be optional, depending on
the command.

X Parameterized Character - A character from the ASCII table within
the range 33-47 decimal (! through /) indicating that the escape
sequence is parameterized.

y Group Character - A character from the ASCII table within the range
96-126 decimal (' through ~) that specifies the group type of control
being performed.

# Value Field - A group of characters specifying a numeric value. The
numeric value is represented as an ASCII string of characters within
the range 48-57 decimal (0 through 9) that may be preceded by a + or
sign and may contain a fractional portion indicated by the digits
after a decimal point ( . ).  Numeric value fields are within the
range -32767 to 65535. If an escape sequence requires a value field
and a value is not specified, a value of zero is assumed.

zi Parameter Character - Any character from the ASCII table within the
range 96-126 decimal ( through ~ ). This character specifies the
parameter to which the previous value field applies. This character is
used when combining escape sequences.

Zn Termination Character - Any character from the ASCII table within
the range 64-94 decimal ( @ through ^ ). This character specifies the
parameter to which the previous value field applies. This character
terminates the escape sequence.

[data] Binary Data is eight-bit data (for example, graphics data, downloaded
fonts, etc.). The number of bytes of binary data is specified by the value
field of the escape sequence. Binary data immediately follows the terminating
character of the escape sequence.
*/

/* Byte code PCL character classification array */
extern
uint8 pcl5_char[256];

#define PCL_2CHAR_CMD         (0x01)
#define PCL_PARAM_CMD         (0x02)
#define PCL_GROUP_CHAR        (0x04)
#define PCL_PARAM_CHAR        (0x08)
#define PCL_TERMINATING_CHAR  (0x10)
#define PCL_CTRL_CODE         (0x20)

/* Macros to quickly test PCL character classification */
#define is_control_code(b)      ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_CTRL_CODE) != 0)
#define is_2char_cmd(b)         ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_2CHAR_CMD) != 0)
#define is_param_cmd(b)         ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_PARAM_CMD) != 0)
#define is_group_char(b)        ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_GROUP_CHAR) != 0)
/* Note, a parameter character may also be a terminating character */
#define is_param_char(b)        ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_PARAM_CHAR) != 0)
/* Note, a terminating character is also a parameter character */
#define is_terminating_char(b)  ((pcl5_char[CAST_UNSIGNED_TO_UINT8(b)]&PCL_TERMINATING_CHAR) != 0)

extern
uint8 make_termination_char(
  uint8 param_char);

int32 pcl5_scan_escape_sequence(PCL5Context *pcl5_ctxt, uint8 *operation) ;

extern
PCL_VALUE pcl_zero_value; /* Default unsigned zero PCL parameter value */

int32 pcl5_scan_params(PCL5Context *pcl5_ctxt, uint8 operation, uint8 *group_char,
                       PCL_VALUE* p_value, uint8 *termination_char) ;

/* ============================================================================
* Log stripped */
#endif
