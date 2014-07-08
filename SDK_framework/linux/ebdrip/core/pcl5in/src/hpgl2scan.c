/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2scan.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scans HPGL2 streams.
 */

#include "core.h"
#include "hqbitops.h"
#include "hpgl2scan.h"
#include "hpgl2dispatch.h"

#include "pcl5context_private.h"
#include "pcl5scan.h"
#include "macros.h"
#include "pictureframe.h"
#include "jobcontrol.h"

#include "ascii.h"
#include "control.h"
#include "float.h"
#include "mm.h" /* mm_alloc */
#include "lowmem.h" /* mm_memory_is_low */
#include "swerrors.h"
#include "swctype.h"
#include "tables.h"
#include "objects.h"
#include "fileio.h"
#include "monitor.h"
#include "display.h"


#define OP_LEN 2
#define HPGL2_MAXINT (0x3fffffff)
#define HPGL2_MININT (-HPGL2_MAXINT)

#define ESCAPE 27

/* Simple cache of last mnemonic seen */
static HPGL2FunctEntry* last_op;
static uint32 last_opcode;

/* Returns -1 on error, 0 if we were unable to scan an op otherwise
   the number of bytes consumed (which will always be 2 for HPGL2
   comamnds). */
/* NOTE - use of last_char has been tuned for performance, please don't move
 * assignments as part of "tidying the code".
 */
static
int32 hpgl2_scan_mnemonic(
  PCL5Context*      pcl5_ctxt,
  HPGL2FunctEntry** hpgl2_op)
{
  int32 ch1, ch2 ;
  FILELIST* flptr;
  uint32 op_cache;

  HQASSERT(hpgl2_op != NULL, "op is NULL") ;
  HQASSERT((last_op != NULL), "hpgl2_scan_mnemonic: cached last op is NULL");

  flptr = pcl5_ctxt->flptr;
  /* Read first byte of mnemonic */
  ch1 = pcl5_ctxt->last_char;
  if (ch1 == EOF) {
    return 0;
  }
  if ( !isalpha(ch1) ) {
    return 0;
  }
  ch1 = toupper(ch1) ;

  /* Read second byte of mnemonic, but note we have now consumed 1 byte */
  if ((ch2 = Getc(flptr)) == EOF) {
    pcl5_ctxt->last_char = ch2;
    return 1;
  }
  if ( !isalpha(ch2) ) {
    pcl5_ctxt->last_char = ch2;
    return 1;
  }
  ch2 = toupper(ch2) ;

  /* Check if opcode is repeat of previous one */
  op_cache = ((uint8)ch1 << 8) | (uint8)ch2;
  if ( op_cache == last_opcode ) {
    *hpgl2_op = last_op;
#if defined(DEBUG_BUILD)
    if ( debug_pcl5 & PCL5_CONTROL ) {
      monitorf((uint8*)"   %c%c", (uint8)ch1, (uint8)ch2);
    }
#endif /* DEBUG_BUILD */
    pcl5_ctxt->last_char = Getc(flptr);
    return OP_LEN;
  }

  if ( (*hpgl2_op = hpgl2_get_op(pcl5_ctxt, (uint8)ch1, (uint8)ch2)) != NULL ) {
    /* Update simple cache details */
    last_opcode = op_cache;
    last_op = *hpgl2_op;
#if defined(DEBUG_BUILD)
    if ( debug_pcl5 & PCL5_CONTROL ) {
      monitorf((uint8*)"   %c%c", (uint8)ch1, (uint8)ch2);
    }
#endif /* DEBUG_BUILD */
    pcl5_ctxt->last_char = Getc(flptr);
    return OP_LEN;
  }

#if defined(DEBUG_BUILD)
  if ( debug_pcl5 & PCL5_CONTROL ) {
    monitorf((uint8*)"   %c%c - unknown op", (uint8)ch1, (uint8)ch2);
  }
#endif /* DEBUG_BUILD */

  /* When two bytes don't make a valid operator mnemonic we still need to put
   * back the second char, as this char, along with the following char, might
   * make a valid mnemonic. */
  pcl5_ctxt->last_char = ch2;
  return(1);
}

/* Returns -1 on error, otherwise the number of bytes consumed.
   The first char, an ESCAPE, should already have been consumeed. */
static
int32 hpgl2_scan_end_of_hpgl2(PCL5Context *pcl5_ctxt,
                              PCL5Operator *op, PCL5Numeric *arg)
{
  int32 ch ;
  int32 arg_bytes, total_bytes = 0 ;

  *op = NULL ;
  arg->integer = -1 ; /* initialise arg to an arbitrary value */
  arg->real = -1 ;

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return 0 ;

  if ( ch != '%' && ch != 'E' && ch != 'e' ) {
    return 0 ;
  }

  ++total_bytes ;

  if ( ch == 'E' || ch == 'e' ) {
    /* Scanned Esc E command. */
    *op = pcl5op_E ;
    return total_bytes ;
  }

  /* Sets up next char for whatever happens next */
  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);

  arg_bytes = hpgl2_scan_real(pcl5_ctxt, &arg->real) ;

  if ( arg_bytes > 0 )
    total_bytes += arg_bytes ;

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return 0 ;

  switch ( ch ) {
  case 'A' : case 'a' :
    /* Scanned %A command. */
    ++total_bytes ;

    if ( arg_bytes <= 0 )
      arg->real = 0 ; /* default value for %A */

    *op = pcl5op_percent_A ;
    break ;
  case 'X' : case 'x' :
    /* Look for UEL command. */
    ++total_bytes ;

    if ( arg_bytes <= 0 || arg->real != -12345.0 )
      return 0 ;

    *op = pcl5op_percent_X ;
    break ;
  default :
    return 0 ;
  }

  return total_bytes ;
}

/* Operator character is [a-zA-Z] */
#define IS_OP_CHAR(_ch)   (isalpha(_ch))

/* Note: does not include OP char which is also a terminator. */
#define IS_SIMPLE_TERMINATOR(_ch) \
      ( ((_ch) == ';') || ((_ch) == SPACE) || ((_ch) == TAB) )

/* See header for doc. */
int32 hpgl2_scan_terminator(PCL5Context *pcl5_ctxt, uint8 *terminator)
{
  FILELIST* flptr;
  int32 ch ;
  int32 terminator_found ;

  flptr = pcl5_ctxt->flptr;
  ch = pcl5_ctxt->last_char;
  if (ch == EOF) {
    /* EOF is a terminator. */
    *terminator = (uint8)EOF ;
    pcl5_ctxt->last_char = Getc(flptr);
    return 1;
  }

  if ( IS_SIMPLE_TERMINATOR(ch) ) {
    *terminator = (uint8)ch ;
    pcl5_ctxt->last_char = Getc(flptr);
    return 1;
  }

  terminator_found = 0;
  if ( IS_OP_CHAR(ch) || ch == ESC ) {
    /* Start of next operator or an Esc act as terminators. */
    terminator_found = 1;
    *terminator = (uint8)ch;
  }
  return terminator_found ;
}

/* See header for doc. */
int32 hpgl2_scan_separator(
  PCL5Context*  pcl5_ctxt)
{
  FILELIST* flptr;
  int32 ch;
  int32 separator_found;

  separator_found = 0;
  flptr = pcl5_ctxt->flptr;
  ch = pcl5_ctxt->last_char;
  while (ch != EOF) {
    if ( ch == ',' || ch == SPACE ) {
      separator_found = 1;
      ch = Getc(flptr);
      continue;
    }
    if ( !separator_found && (ch == '-' || ch == '+') ) {
      separator_found = 1;
    }
    break;
  }
  pcl5_ctxt->last_char = ch;
  return separator_found;
}

#ifdef DEBUG_BUILD
#undef HPGL2_SCAN_STATS
#endif /* DEBUG_BUILD */

#ifdef HPGL2_SCAN_STATS
uint32 hpgl2_real_scan;
uint32 hpgl2_real_sign;
uint32 hpgl2_real_integer;
uint32 hpgl2_real_real;
#define DEBUG_COUNT(v)  MACRO_START (v)++; MACRO_END
#else /* !HPGL2_SCAN_STATS */
#define DEBUG_COUNT(v)
#endif /* !HPGL2_SCAN_STATS */

/* NOTE - use of last_char has been tuned for performance, please don't move
 * assignments as part of "tidying the code".
 */
int32 hpgl2_scan_real(
  PCL5Context*  pcl5_ctxt,
  HPGL2Real*    value)
{
  double fp;
  FILELIST* flptr;
  int32 ch;
  int32 sign;
  int32 integer;
  int32 fraction;
  int32 ndigits;
  Bool consumed = FALSE;

  DEBUG_COUNT(hpgl2_real_scan);

  ch = pcl5_ctxt->last_char;
  if (ch == EOF)
    return 0 ;
  flptr = pcl5_ctxt->flptr;
  /* Handle any leading sign */
  sign = 0;
  if ( !isdigit(ch) ) {
    if ( ch == '-' ) {
      DEBUG_COUNT(hpgl2_real_sign);
      sign = -1;
      if ((ch = Getc(flptr)) == EOF) {
        pcl5_ctxt->last_char = ch;
        return 0 ;
      }
    } else if ( ch == '+' ) {
      DEBUG_COUNT(hpgl2_real_sign);
      if ((ch = Getc(flptr)) == EOF) {
        pcl5_ctxt->last_char = ch;
        return 0 ;
      }
    }
  }

  /* Read integral part */
  integer = 0;
  if ( isdigit (ch) ) {
    consumed = TRUE;
    do {
      integer = integer*10 + (ch - '0');
      ch = Getc(flptr);
    } while ( (ch != EOF) && isdigit(ch) );
  }

  INLINE_MIN32(integer, integer, HPGL2_MAXINT);

  /* If no separator then return integer value */
  if ( ch != '.' ) {
    pcl5_ctxt->last_char = ch;
    integer = (integer ^ sign) - sign; /* Apply sign to value */
    *value = (HPGL2Real)integer;
    DEBUG_COUNT(hpgl2_real_integer);
    return(consumed);
  }

  DEBUG_COUNT(hpgl2_real_real);
  /* Scan any fractional value */
  fraction = 0;
  ndigits = 0;
  if ((ch = Getc(flptr)) != EOF) {
    while ( isdigit(ch) ) {
      ndigits++;
      fraction = fraction*10 + ch - '0';
      if ((ch = Getc(flptr)) == EOF) {
        break;
      }
    }
  }
  pcl5_ctxt->last_char = ch;

  /* No fractional part, return just integer */
  if ( ndigits == 0 ) {
    integer = (integer ^ sign) - sign; /* Apply sign to value */
    *value = (HPGL2Real)integer;
    DEBUG_COUNT(hpgl2_real_integer);
    return(1);
  }
  /* Construct real value from sign, integer and fraction */
  if ( ndigits < 10 ) {
    static double fdivs[10] = {
      0.0, 0.1, 0.01, 0.001, 0.0001, 0.00001, 0.000001, 0.0000001, 0.00000001, 0.000000001
    };
    fp = integer + (fraction*fdivs[ndigits]);
  } else {
    fp = integer + (fraction/pow(10.0, ndigits));
  }
  if ( sign < 0 ) {
    fp = -fp;
  }
  *value = fp;
  return(1);
}

/* Returns -1 on error, 0 if we were unable to scan otherwise the
   number of bytes consumed. */
int32 hpgl2_scan_clamped_real(PCL5Context *pcl5_ctxt, HPGL2Real *value)
{
  int32 bytes ;

  bytes = hpgl2_scan_real(pcl5_ctxt, value) ;
  if ( bytes < 0 )
    return -1 ;

  /* Clamp value to [ -32768 32767 ] */
  if ( *value > MAXINT16 )
    *value = MAXINT16 ;
  else if ( *value < MININT16 )
    *value = MININT16 ;

  return bytes ;
}

/* Returns -1 on error, 0 if we were unable to scan otherwise the
   number of bytes consumed. */
int32 hpgl2_scan_point(PCL5Context *pcl5_ctxt, HPGL2Point *point)
{
  int32 bytes ;

  point->x = 0 ;
  point->y = 0 ;

  bytes = hpgl2_scan_real(pcl5_ctxt, &point->x) ;
  if ( bytes < 0 )
    return -1 ;

  (void)hpgl2_scan_separator(pcl5_ctxt) ;

  bytes = hpgl2_scan_real(pcl5_ctxt, &point->y) ;

  return bytes ;
}

/* Returns -1 on error, 0 if we were unable to scan otherwise the
   number of bytes consumed. */
int32 hpgl2_scan_integer(PCL5Context *pcl5_ctxt, HPGL2Integer *value)
{
  HPGL2Real real_value ;
  int32 res ;

  if ((res = hpgl2_scan_real(pcl5_ctxt, &real_value)) < 0)
    return -1 ;

  /* Integer must be clamped to [ -1073741823 1073741823 ] */
  HQASSERT(HPGL2_MININT <= real_value && real_value <= HPGL2_MAXINT,
           "HPGL2 integer has not been clamped to required range") ;

  /* Spec says that when an integer is expected but we find a real,
     the value gets rounded to the nearest integer. */
  *value = (HPGL2Integer)real_value ;

  return res ;
}

/* Returns -1 on error, 0 if we were unable to scan otherwise the
   number of bytes consumed. */
int32 hpgl2_scan_clamped_integer(PCL5Context *pcl5_ctxt, HPGL2Integer *value)
{
  HPGL2Real real_value ;
  int32 res ;

  if ((res = hpgl2_scan_real(pcl5_ctxt, &real_value)) < 0)
    return -1 ;

  /* Spec says that when an integer is expected but we find a real,
     the value gets rounded to the nearest integer. */
  *value = (HPGL2Integer)real_value ;

  /* Clamp value to [ -32768 32767 ] */
  if ( *value > MAXINT16 )
    *value = MAXINT16 ;
  else if ( *value < MININT16 )
    *value = MININT16 ;

  return res ;
}

/**
 * \todo This function is currently not used, but is left here as a reminder of
 *       how we may improve on scanning; currently most clients of the scan
 *       methods in this file ignore error returns because they are conflated
 *       with valid return values - e.g. a function returns -1 on error, 0 if
 *       the requested item was not scanned, or > 0 if it was. Most code treats
 *       -1 and 0 as the same, which is incorrect. - richardb 2008/4/17
 *
 * Returns -1 on error, 0 if we were unable to scan any parameters otherwise
 * the number of bytes consumed.
 */
#if 0
enum {
  HPGL2_INT = 0,
  HPGL2_CLAMPED_INT,
  HPGL2_REAL,
  HPGL2_CLAMPED_REAL
} ;

/* optional in the match end must be TRUE otherwise match function
   will fail */
#define HPGL2_PARAMS_MATCH_END {-1, TRUE, NULL, NULL}

typedef struct HPGL2PARAMS {
  int32 type ;
  Bool optional ;
  Bool *found ;
  void *data ;
} HPGL2PARAMS ;

int32 hpgl2_match(PCL5Context *pcl5_ctxt, HPGL2PARAMS *params, uint8 *terminator)
{
  HPGL2PARAMS *p ;
  Bool seen_optional = FALSE ;
  int32 res = 0 ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;
  HQASSERT(params != NULL, "params is NULL") ;

  p = params ;
  while (p->type != -1) {
    if (! seen_optional && p->optional) {
      seen_optional = TRUE ;
    } else if (seen_optional && p->optional) {
      HQFAIL("All params to the right of first optional MUST be optional") ;
      return -1 ;
    }

    switch (p->type) {
    case HPGL2_INT:
      if ((res = hpgl2_scan_integer(pcl5_ctxt, (HPGL2Integer*)p->data)) < 0)
        return -1 ;
      break ;
    case HPGL2_CLAMPED_INT:
      if ((res = hpgl2_scan_clamped_integer(pcl5_ctxt, (HPGL2Integer*)p->data)) < 0)
        return -1 ;
      break ;
    case HPGL2_REAL:
      if ((res = hpgl2_scan_real(pcl5_ctxt, (HPGL2Real*)p->data)) < 0)
        return -1 ;
      break ;
    case HPGL2_CLAMPED_REAL:
      if ((res = hpgl2_scan_clamped_real(pcl5_ctxt, (HPGL2Real*)p->data)) < 0)
        return -1 ;
      break ;
    default:
      HQFAIL("Invalid param type") ;
      break ;
    }
    if (res == 0) {
      *p->found = FALSE ;
    } else {
      *p->found = TRUE ;
    }
    /* If the param is not optional and we did not scan it, abort
       out. */
    if (! p->optional && ! *p->found)
      return 0 ;

    p++ ;
  }

  if (hpgl2_scan_terminator(pcl5_ctxt, terminator) < 0)
    return -1 ;

  return res ;
}
#endif /* if 0 */

#define COMMENT_DELIMITER '"'
/* Returns -1 on EOF, 0 if no lead delimiter, else 1. */
int32 hpgl2_scan_comment(PCL5Context *pcl5_ctxt)
{
  FILELIST* flptr;
  int32 ch;

  ch = pcl5_ctxt->last_char;
  if (ch != EOF) {
    /* Check for lead comment delimiter */
    if ( ch != COMMENT_DELIMITER ) {
      return(0);
    }
    /* Look for end comment delimiter */
    flptr = pcl5_ctxt->flptr;
    while ((ch = Getc(flptr)) != EOF && (ch != COMMENT_DELIMITER) ) {
      EMPTY_STATEMENT();
    }
    pcl5_ctxt->last_char = ch;
  }
  if ( ch == COMMENT_DELIMITER ) {
    return(1);
  }
  return(-1);
}

Bool hpgl2op_nullop(PCL5Context *pcl5_ctxt)
{
  FILELIST* flptr;

  HQASSERT(pcl5_ctxt != NULL, "Bad PCL5Context");

  if (pcl5_ctxt != NULL) {
    uint8 terminator;

    flptr = pcl5_ctxt->flptr;
    while (hpgl2_scan_terminator(pcl5_ctxt, &terminator) <= 0)
      pcl5_ctxt->last_char = Getc(flptr) ;
  }

  return TRUE;
}

/* Start interpreting the provided stream as HPGL2. Make sure
   hpgl2_cleanup() is called before any return from this function. */
Bool hpgl2_execops(PCL5Context *pcl5_ctxt)
{
  int32 res;
  HPGL2FunctEntry* op;
  HPGL2Real value;
  uint8 terminator;
  int saved_dl_safe_recursion = dl_safe_recursion ;
  int saved_gc_safety_level = gc_safety_level;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  pcl5_ctxt->end_of_hpgl2_op = NULL ;
  pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);

  /* Prime mnemonic cache with known good operator */
  last_opcode = ('I' << 8) | 'N';
  last_op = hpgl2_get_op(pcl5_ctxt, (uint8)'I', (uint8)'N');

  for (;;) {
    /* Prime the last_char cache for HP-GL/2 scanner functions */
    while ( (res = hpgl2_scan_mnemonic(pcl5_ctxt, &op)) == OP_LEN ) {
      gc_unsafe_from_here_on();
      /* Dispatch the function. Leave the parameter parsing code in to
       * catch the parameters for unimplemented functions.
       */
      if ( !hpgl2_dispatch_op(pcl5_ctxt, op) ) {
        UnGetc(pcl5_ctxt->last_char, pcl5_ctxt->flptr);
        return FALSE;
      }

      /* Expect terminator next, but if not consume outstanding params */
      /** \todo MRW, why not just scan for a terminator? */
      while ( hpgl2_scan_terminator(pcl5_ctxt, &terminator) == 0 ) {
        if ( (hpgl2_scan_separator(pcl5_ctxt) | hpgl2_scan_real(pcl5_ctxt, &value)) == 0 ) {
          break;
        }
#if defined(DEBUG_BUILD)
        if ( debug_pcl5 & PCL5_CONTROL ) {
          monitorf((uint8*)" %f", value) ;
        }
#endif /* DEBUG_BUILD */
      }
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"\n") ;
      }
#endif /* DEBUG_BUILD */

      dl_safe_recursion = saved_dl_safe_recursion ;
      gc_safety_level = saved_gc_safety_level;

      /* Handle low memory, interrupts, timeouts, etc. */
      if ( mm_memory_is_low || dosomeaction ) {
        if ( !handleNormalAction() ) {
          UnGetc(pcl5_ctxt->last_char, pcl5_ctxt->flptr);
          return FALSE ;
        }
      }
    }

    if (res < 0) {
      UnGetc(pcl5_ctxt->last_char, pcl5_ctxt->flptr);
      return FALSE ;
    }

    /* We switch back to PCL if we find the end of the input stream. */
    if ( isIEof(pcl5_ctxt->flptr) )
      return TRUE ;

    if (res == 0 || pcl5_ctxt->last_char == ESCAPE) {
      int32 ch ;
      /* Either scan an enter-PCL-command or just skip this char
         (whitespace or garbage). So, never need to unget this
         char. */
      ch = pcl5_ctxt->last_char;
      if (ch == EOF)
        return FALSE ;

      if ( ch == ESCAPE ) {
        pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
        if ( (res = hpgl2_scan_end_of_hpgl2(pcl5_ctxt,
                                            &pcl5_ctxt->end_of_hpgl2_op,
                                            &pcl5_ctxt->end_of_hpgl2_arg)) < 0 )
          return TRUE ;

        if ( res > 0 ) {
          /* Scanned an end-of-hpgl-mode command; we need to leave HPGL. */
          return TRUE ;
        }
      }
      /* We switch back to PCL if we find the end of the input stream. */
      if ( isIEof(pcl5_ctxt->flptr) )
        return TRUE ;
      pcl5_ctxt->last_char = Getc(pcl5_ctxt->flptr);
    }
  }
  /* NEVERREACHED */
}

void init_C_globals_hpgl2scan(void)
{
  last_op = NULL ;
  last_opcode = 0 ;
#ifdef HPGL2_SCAN_STATS
  hpgl2_real_scan = 0 ;
  hpgl2_real_sign = 0;
  hpgl2_real_integer = 0;
  hpgl2_real_real = 0;
#endif
}

/* ============================================================================
* Log stripped */
