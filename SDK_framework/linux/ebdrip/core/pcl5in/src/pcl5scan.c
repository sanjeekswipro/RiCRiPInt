/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5scan.c(EBDSDK_P.1) $
 * $Id: src:pcl5scan.c,v 1.36.4.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scans PCL5 streams.
 */

#include <math.h>

#include "core.h"
#include "pcl5scan.h"

#include "pcl5context_private.h"
#include "macros.h"

#include "ascii.h"
#include "float.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "swctype.h"
#include "tables.h"
#include "objects.h"
#include "fileio.h"

#define IS_PCL_WHITESPACE(ch) \
  ((ch) == ' ' || (ch) == '\t')

/* PCL byte classification table */
uint8 pcl5_char[256] = {
  PCL_CTRL_CODE, /* NUL */
  0, /* SOH */
  0, /* STX */
  0, /* ETX */
  0, /* EOT */
  0, /* ENQ */
  0, /* ACK */
  PCL_CTRL_CODE, /* BEL */
  PCL_CTRL_CODE, /* BS */
  PCL_CTRL_CODE, /* HT */
  PCL_CTRL_CODE, /* LF */
  PCL_CTRL_CODE, /* VT */
  PCL_CTRL_CODE, /* FF */
  PCL_CTRL_CODE, /* CR */
  0, /* S0 */
  0, /* SI */
  0, /* DLE */
  0, /* DC1 */
  0, /* DC2 */
  0, /* DC3 */
  0, /* DC4 */
  0, /* NAK */
  0, /* SYN */
  0, /* ETB */
  0, /* CAN */
  0, /* EM */
  0, /* SUB */
  PCL_CTRL_CODE, /* ESC */
  0, /* FS */
  0, /* GS */
  0, /* RS */
  0, /* US */
  PCL_CTRL_CODE, /* SP */
  PCL_PARAM_CMD, /* ! */
  PCL_PARAM_CMD, /* " */
  PCL_PARAM_CMD, /* # */
  PCL_PARAM_CMD, /* $ */
  PCL_PARAM_CMD, /* % */
  PCL_PARAM_CMD, /* & */
  PCL_PARAM_CMD, /* ' */
  PCL_PARAM_CMD, /* ( */
  PCL_PARAM_CMD, /* ) */
  PCL_PARAM_CMD, /* * */
  PCL_PARAM_CMD, /* + */
  PCL_PARAM_CMD, /* , */
  PCL_PARAM_CMD, /* - */
  PCL_PARAM_CMD, /* . */
  PCL_PARAM_CMD, /* / */
  PCL_2CHAR_CMD, /* 0 */
  PCL_2CHAR_CMD, /* 1 */
  PCL_2CHAR_CMD, /* 2 */
  PCL_2CHAR_CMD, /* 3 */
  PCL_2CHAR_CMD, /* 4 */
  PCL_2CHAR_CMD, /* 5 */
  PCL_2CHAR_CMD, /* 6 */
  PCL_2CHAR_CMD, /* 7 */
  PCL_2CHAR_CMD, /* 8 */
  PCL_2CHAR_CMD, /* 9 */
  PCL_2CHAR_CMD, /* : */
  PCL_2CHAR_CMD, /* ; */
  PCL_2CHAR_CMD, /* < */
  PCL_2CHAR_CMD, /* = */
  PCL_2CHAR_CMD, /* > */
  PCL_2CHAR_CMD, /* ? */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* @ */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* A */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* B */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* C */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* D */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* E */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* F */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* G */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* H */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* I */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* J */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* K */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* L */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* M */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* N */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* O */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* P */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* Q */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* R */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* S */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* T */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* U */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* V */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* W */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* X */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* Y */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* Z */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* [ */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* \ */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* ] */
  PCL_2CHAR_CMD|PCL_PARAM_CHAR|PCL_TERMINATING_CHAR, /* ^ */
  PCL_2CHAR_CMD, /* _ */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* ` */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* a */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* b */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* c */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* d */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* e */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* f */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* g */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* h */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* i */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* j */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* k */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* l */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* m */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* n */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* o */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* p */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* q */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* r */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* s */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* t */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* u */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* v */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* w */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* x */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* y */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* z */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* { */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* | */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* } */
  PCL_2CHAR_CMD|PCL_GROUP_CHAR|PCL_PARAM_CHAR, /* ~ */
  0, /* DEL */
  0, /* 0x80 */
  0, /* 0x81 */
  0, /* 0x82 */
  0, /* 0x83 */
  0, /* 0x84 */
  0, /* 0x85 */
  0, /* 0x86 */
  0, /* 0x87 */
  0, /* 0x88 */
  0, /* 0x89 */
  0, /* 0x8a */
  0, /* 0x8b */
  0, /* 0x8c */
  0, /* 0x8d */
  0, /* 0x8e */
  0, /* 0x8f */
  0, /* 0x90 */
  0, /* 0x91 */
  0, /* 0x92 */
  0, /* 0x93 */
  0, /* 0x94 */
  0, /* 0x95 */
  0, /* 0x96 */
  0, /* 0x97 */
  0, /* 0x98 */
  0, /* 0x99 */
  0, /* 0x9a */
  0, /* 0x9b */
  0, /* 0x9c */
  0, /* 0x9d */
  0, /* 0x9e */
  0, /* 0x9f */
  0, /* 0xa0 */
  0, /* 0xa1 */
  0, /* 0xa2 */
  0, /* 0xa3 */
  0, /* 0xa4 */
  0, /* 0xa5 */
  0, /* 0xa6 */
  0, /* 0xa7 */
  0, /* 0xa8 */
  0, /* 0xa9 */
  0, /* 0xaa */
  0, /* 0xab */
  0, /* 0xac */
  0, /* 0xad */
  0, /* 0xae */
  0, /* 0xaf */
  0, /* 0xb0 */
  0, /* 0xb1 */
  0, /* 0xb2 */
  0, /* 0xb3 */
  0, /* 0xb4 */
  0, /* 0xb5 */
  0, /* 0xb6 */
  0, /* 0xb7 */
  0, /* 0xb8 */
  0, /* 0xb9 */
  0, /* 0xba */
  0, /* 0xbb */
  0, /* 0xbc */
  0, /* 0xbd */
  0, /* 0xbe */
  0, /* 0xbf */
  0, /* 0xc0 */
  0, /* 0xc1 */
  0, /* 0xc2 */
  0, /* 0xc3 */
  0, /* 0xc4 */
  0, /* 0xc5 */
  0, /* 0xc6 */
  0, /* 0xc7 */
  0, /* 0xc8 */
  0, /* 0xc9 */
  0, /* 0xca */
  0, /* 0xcb */
  0, /* 0xcc */
  0, /* 0xcd */
  0, /* 0xce */
  0, /* 0xcf */
  0, /* 0xd0 */
  0, /* 0xd1 */
  0, /* 0xd2 */
  0, /* 0xd3 */
  0, /* 0xd4 */
  0, /* 0xd5 */
  0, /* 0xd6 */
  0, /* 0xd7 */
  0, /* 0xd8 */
  0, /* 0xd9 */
  0, /* 0xda */
  0, /* 0xdb */
  0, /* 0xdc */
  0, /* 0xdd */
  0, /* 0xde */
  0, /* 0xdf */
  0, /* 0xe0 */
  0, /* 0xe1 */
  0, /* 0xe2 */
  0, /* 0xe3 */
  0, /* 0xe4 */
  0, /* 0xe5 */
  0, /* 0xe6 */
  0, /* 0xe7 */
  0, /* 0xe8 */
  0, /* 0xe9 */
  0, /* 0xea */
  0, /* 0xeb */
  0, /* 0xec */
  0, /* 0xed */
  0, /* 0xee */
  0, /* 0xef */
  0, /* 0xf0 */
  0, /* 0xf1 */
  0, /* 0xf2 */
  0, /* 0xf3 */
  0, /* 0xf4 */
  0, /* 0xf5 */
  0, /* 0xf6 */
  0, /* 0xf7 */
  0, /* 0xf8 */
  0, /* 0xf9 */
  0, /* 0xfa */
  0, /* 0xfb */
  0, /* 0xfc */
  0, /* 0xfd */
  0, /* 0xfe */
  0  /* 0xff */
};

/* Convert a PCL parameterised command parameter character into a termination
 * character */
uint8 make_termination_char(
  uint8 param_char)
{
  HQASSERT((is_param_char(param_char)),
           "make_termination_char: not a valid parameter char");

  return(param_char - 32);

} /* make_termination_char */


static double power[6] = {1, 10, 100, 1000, 10000, 100000};

/* Round a real value to n decimal places (up to 4) without using cast to int
 * for truncation (avoids possibly integer overflow).
 */

PCL5Real pcl5_round_to_ndp(
  PCL5Real  value,
  uint32    digits)
{
  double fraction;
  double integer;

  HQASSERT((digits < 5),
           "pcl5_round_to_ndp: invalid number of decimal places");

  value *= power[digits + 1];
  value += (value < 0 ? -5 : 5);
  fraction = modf(value/10, &integer);
  value = integer/power[digits];

  return(value);
}

/* Truncate a real value to n decimal places (up to 4) without using cast to int
 * for truncation (avoids possibly integer overflow).
 */
PCL5Real pcl5_truncate_to_ndp(
  PCL5Real  value,
  uint32    digits)
{
  double fraction;
  double integer;

  HQASSERT((digits < 5),
           "pcl5_truncate_to_ndp: invalid number of decimal places");

  value *= power[digits];
  fraction = modf(value, &integer);
  value = integer/power[digits];

  return(value);
}

PCL5Real pcl5_limit_to_range(PCL5Real value, PCL5Real min, PCL5Real max)
{
  HQASSERT(min <= max, "Unexpected range") ;

  if (value < min)
    value = min ;
  else if (value > max)
    value = max ;

  return value ;
}

#define BIGGEST_DOUBLE_DIV_10 (DBL_MAX / 10)

#define BIGGEST_INT_DIV_10 (INT_MAX / 10)

/* Returns -1 on error, 0 if we were unable to scan an escape
   sequence otherwise the number of bytes consumed. */
int32 pcl5_scan_escape_sequence(PCL5Context *pcl5_ctxt, uint8 *operation)
{
  int32 ch ;
  int32 res = 0 ;

  *operation = 0 ;

  /* We have an Ec (escape), start reading PCL command. */
  if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
    /* Do we have a two character escape sequence? */
    if ( is_2char_cmd(ch) ) {
      res++ ;
      *operation = (uint8)ch ;

      /* Do we have a parameterized escape sequence? */
    } else if ( is_param_cmd(ch) ) {
      res++ ;
      *operation = (uint8)ch ;

    } else {
      /** \todo Umm, what do we do when its neither of these? For now,
          unget the char and return TRUE. */
      UnGetc(ch, pcl5_ctxt->flptr) ;
    }
  } else {
    return 0 ;
  }

  return res ;
}

/* Returns -1 on error, 0 if we were unable to scan a group character
   otherwise the number of bytes consumed. */
static
int32 pcl5_scan_group_char(PCL5Context *pcl5_ctxt, uint8 *group_character)
{
  int32 ch ;
  int32 res = 0 ;

  *group_character = 0 ;

  if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
    if ( is_group_char(ch) ) {
      *group_character = (uint8)ch ;
      res = 1 ;
    } else {
      UnGetc(ch, pcl5_ctxt->flptr) ;
    }
  }

  return res ;
}

/* Read and throw out all characters which are not parameter nor
   termination characters. */
static
void consume_non_param_and_non_termination_chars(PCL5Context *pcl5_ctxt)
{
  int32 ch ;
  do {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return ;
  } while ( !(is_param_char(ch) || is_control_code(ch)) );
  UnGetc(ch, pcl5_ctxt->flptr) ;
}

/* Read and throw out all characters which are not termination
   characters. */
static
void consume_non_termination_chars(PCL5Context *pcl5_ctxt)
{
  int32 ch ;
  do {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return ;
  } while ( !(is_terminating_char(ch) || is_control_code(ch)) );
  UnGetc(ch, pcl5_ctxt->flptr) ;
}

PCL_VALUE pcl_zero_value = {
  NOSIGN, FALSE, 0, 0, 0
};

/* Return -2 if we encounter a fatal error. Returns -1 if we encounter
   a scan error, 0 if we were unable to scan a value field at all
   which means fleading etc.. will default to zero, otherwise the
   number of bytes consumed.

   The reason we have a explicit_sign param is so that we can handle
   -0 and +0 which is allowed. Ho hum.

   We scan the leading and trailing digits as integers until we know
   that we expect a real which is determined by scanning the
   termination character. */
static
int32 pcl5_scan_value_field(PCL5Context *pcl5_ctxt,
                            PCL_VALUE*  p_value)
{
  int32 ch ;
  int32 sign ;
  int32 res = 0 ;

  /* If the value field is required but not present, the spec says
     that zero is assumed for value fields. Will assume a non explicit
     positive zero as well. */
  *p_value = pcl_zero_value;

  sign = 0 ;
  do {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return 0 ;
    res++ ;

    if (ch == '-' || ch =='+') {
      if (sign != 0) { /* Multiple sign characters. Throw whole
                          command out. */
        consume_non_termination_chars(pcl5_ctxt) ;
        return -1 ;
      }
      if (ch == '-') {
        sign = -1 ;
        p_value->explicit_sign = EXPLICIT_NEGATIVE ;
      } else {
        sign = 1 ;
        p_value->explicit_sign = EXPLICIT_POSITIVE ;
      }
    }
  } while ( ch == '-' || ch == '+' );

  /* Allow space between the +|- and the first digit. */
  while (IS_PCL_WHITESPACE(ch)) {
    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      return 0 ;
    res++ ;
  }

  /* If no sign has been specified, default to a positive. */
  if (sign == 0)
    sign = 1 ;

  /* Scan the m part of a (m.n) number. */
  while ( isdigit(ch) ) {
    ch = ( ch - '0' ) ;

    /* Avoid int overflow before doing the multiplication but we
       keep scanning. */
    if (BIGGEST_INT_DIV_10 >= (p_value->fleading))
      (p_value->fleading) = 10 * (p_value->fleading) + ch ;

    if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
      break ;
    res++ ;
  }

  if (ch == '.') {
    p_value->decimal_place = TRUE ;

    if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
      res++ ;

      /* Scan the n part of a (m.n) number. */
      while ( isdigit(ch) ) {
        ch = ( ch - '0' ) ;
        ++(p_value->ntrailing) ;

        /* Avoid int overflow before doing the multiplication but we
           keep scanning. */
        if (BIGGEST_INT_DIV_10 >= (p_value->ftrailing)) {
          (p_value->ftrailing) = 10 * (p_value->ftrailing) + ch ;
        } else {
          --(p_value->ntrailing) ;
        }

        if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
          break ;
        res++ ;
      }
    }
  }

  if ( sign < 0 ) {
    if (p_value->fleading == 0) {
      (p_value->ftrailing) = -(p_value->ftrailing) ;
    } else {
      (p_value->fleading) = -(p_value->fleading) ;
    }
  }

  if (ch != EOF) {
    UnGetc(ch, pcl5_ctxt->flptr) ;

    if (! is_param_char(ch))
      consume_non_param_and_non_termination_chars(pcl5_ctxt) ;
  }
  /* We need to decrement the character count if ch is EOF or not. */
  res-- ;

  return res ;
}

/* Returns -1 on error, 0 if we were unable to scan a termination
   character otherwise the number of bytes consumed. */
static
int32 pcl5_scan_termination_char(PCL5Context *pcl5_ctxt, uint8 *termination_char)
{
  int32 ch ;
  int32 res = 0 ;

  *termination_char = 0 ;

  if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
    if ( is_terminating_char(ch) ) {
      *termination_char = (uint8)ch ;
      res = 1 ;
    } else {
      UnGetc(ch, pcl5_ctxt->flptr) ;
    }
  }

  return res ;
}

/* Returns -1 on error, 0 if we were unable to scan a parameter
   character otherwise the number of bytes consumed. */
static
int32 pcl5_scan_parameter_char(PCL5Context *pcl5_ctxt, uint8 *parameter_char)
{
  int32 ch ;
  int32 res = 0 ;

  *parameter_char = 0 ;

  if ((ch = Getc(pcl5_ctxt->flptr)) != EOF) {
    if ( is_param_char(ch) ) {
      *parameter_char = (uint8)ch ;
      res = 1 ;
    } else {
      UnGetc(ch, pcl5_ctxt->flptr) ;
    }
  }

  return res ;
}

/* Returns -1 on error, 0 if we were unable to scan params otherwise
   the number of bytes consumed. */
int32 pcl5_scan_params(PCL5Context *pcl5_ctxt, uint8 operation, uint8 *group_char,
                       PCL_VALUE* p_value, uint8 *termination_char)
{
  int32 total_bytes = 0 ;
  int32 res ;
  uint8 parameter_char ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* We only look for a group char if we are not reading a combined
     escape sequence. */
  if (! pcl5_ctxt->is_combined_command) {
    if ( (res = pcl5_scan_group_char(pcl5_ctxt, group_char)) < 0)
      return -1 ;
    total_bytes += res ;
  }

  /* If res is zero, it means that value will be zero so continue as
     that is the PCL5 default when no parameter is specified. -1 is
     not a fatal error but merely a scan error which we cope with. It
     also means that the input will have been scanned upto the the
     termination character. -2 is a fatal error and hence we need to
     bomb out. */
  if ( (res = pcl5_scan_value_field(pcl5_ctxt, p_value)) < -1) {
    return -1 ;
  } else if (res == -1) {
    if ( (res = pcl5_scan_termination_char(pcl5_ctxt,  termination_char)) < 0)
      return -1 ;
    return 0 ;
  }

  total_bytes += res ;

  if ( (res = pcl5_scan_termination_char(pcl5_ctxt, termination_char)) < 0)
    return -1 ;
  total_bytes += res ;

  /* If no termination character, do we have a parameter char? */
  if (res == 0) {
    if ( (res = pcl5_scan_parameter_char(pcl5_ctxt, &parameter_char)) < 0)
      return -1 ;

    /* We have a parameter char. */
    if (res > 0) {
      total_bytes += res ;
      *termination_char = make_termination_char(parameter_char);

      pcl5_ctxt->is_combined_command = TRUE ;
      pcl5_ctxt->cached_operation = operation ;
      pcl5_ctxt->cached_group_char = *group_char ;
    }

  } else { /* We have a termination char. */
    pcl5_ctxt->is_combined_command = FALSE ;
  }

  return total_bytes ;
}

/* ============================================================================
* Log stripped */
