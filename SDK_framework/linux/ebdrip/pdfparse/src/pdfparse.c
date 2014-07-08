/** \file
 * \ingroup pdfparse
 *
 * $HopeName: SWpdfparse!src:pdfparse.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \todo
 * - parse state machine needs final check
 * - consideration of detecting and handling mal-formed PDFs
 * - handling of unexpected tokens!!!!
 *   . many objects within obj ... endobj
 */

/**
 * \defgroup pdfparse PDF parsing and scanning for RIP skin
 */

#include "pdfparse.h"

#include "hqmemcmp.h"
#include "hqmemcpy.h"


/* Bit flags for types of PDF job characters */
/* A 'regular' PDF token character */
#define REGULAR               (0x01)
/* Delimiters are (), <>, [], {}, /, and % */
#define DELIMITER             (0x02)
/* WS are NUL, HT, LF, FF, CR, and SP */
#define WHITESPACE            (0x04)
/* Digits are 0 thru 9 */
#define DIGIT                 (0x08)
/* CR and LF */
#define EOL                   (0x10)
/* Hexdigit - 0-9, a-f, A-F */
#define HEXDIGIT              (0x20)

/* Byte value classification table */
static
uint8 pdf_ctype[256] = {
  /* 0x00 */  WHITESPACE,                 /* NUL */
  /* 0x01 */  REGULAR,
  /* 0x02 */  REGULAR,
  /* 0x03 */  REGULAR,
  /* 0x04 */  REGULAR,
  /* 0x05 */  REGULAR,
  /* 0x06 */  REGULAR,
  /* 0x07 */  REGULAR,
  /* 0x08 */  REGULAR,
  /* 0x09 */  WHITESPACE,                 /* HT */
  /* 0x0a */  WHITESPACE | EOL,           /* LF */
  /* 0x0b */  REGULAR,
  /* 0x0c */  WHITESPACE,                 /* FF */
  /* 0x0d */  WHITESPACE | EOL,           /* CR */
  /* 0x0e */  REGULAR,
  /* 0x0f */  REGULAR,
  /* 0x10 */  REGULAR,
  /* 0x11 */  REGULAR,
  /* 0x12 */  REGULAR,
  /* 0x13 */  REGULAR,
  /* 0x14 */  REGULAR,
  /* 0x15 */  REGULAR,
  /* 0x16 */  REGULAR,
  /* 0x17 */  REGULAR,
  /* 0x18 */  REGULAR,
  /* 0x19 */  REGULAR,
  /* 0x1a */  REGULAR,
  /* 0x1b */  REGULAR,
  /* 0x1c */  REGULAR,
  /* 0x1d */  REGULAR,
  /* 0x1e */  REGULAR,
  /* 0x1f */  REGULAR,
  /* 0x20 */  WHITESPACE,                 /* SP */
  /* 0x21 */  REGULAR,
  /* 0x22 */  REGULAR,
  /* 0x23 */  REGULAR,
  /* 0x24 */  REGULAR,
  /* 0x25 */  DELIMITER,                  /* % */
  /* 0x26 */  REGULAR,
  /* 0x27 */  REGULAR,
  /* 0x28 */  DELIMITER,                  /* ( */
  /* 0x29 */  DELIMITER,                  /* ) */
  /* 0x2a */  REGULAR,
  /* 0x2b */  REGULAR,
  /* 0x2c */  REGULAR,
  /* 0x2d */  REGULAR,
  /* 0x2e */  REGULAR,
  /* 0x2f */  DELIMITER,                  /* / */
  /* 0x30 */  REGULAR | DIGIT | HEXDIGIT, /* 0 */
  /* 0x31 */  REGULAR | DIGIT | HEXDIGIT, /* 1 */
  /* 0x32 */  REGULAR | DIGIT | HEXDIGIT, /* 2 */
  /* 0x33 */  REGULAR | DIGIT | HEXDIGIT, /* 3 */
  /* 0x34 */  REGULAR | DIGIT | HEXDIGIT, /* 4 */
  /* 0x35 */  REGULAR | DIGIT | HEXDIGIT, /* 5 */
  /* 0x36 */  REGULAR | DIGIT | HEXDIGIT, /* 6 */
  /* 0x37 */  REGULAR | DIGIT | HEXDIGIT, /* 7 */
  /* 0x38 */  REGULAR | DIGIT | HEXDIGIT, /* 8 */
  /* 0x39 */  REGULAR | DIGIT | HEXDIGIT, /* 9 */
  /* 0x3a */  REGULAR,
  /* 0x3b */  REGULAR,
  /* 0x3c */  DELIMITER,                  /* < */
  /* 0x3d */  REGULAR,
  /* 0x3e */  DELIMITER,                  /* > */
  /* 0x3f */  REGULAR,
  /* 0x40 */  REGULAR,
  /* 0x41 */  REGULAR | HEXDIGIT,         /* A */
  /* 0x42 */  REGULAR | HEXDIGIT,         /* B */
  /* 0x43 */  REGULAR | HEXDIGIT,         /* C */
  /* 0x44 */  REGULAR | HEXDIGIT,         /* D */
  /* 0x45 */  REGULAR | HEXDIGIT,         /* E */
  /* 0x46 */  REGULAR | HEXDIGIT,         /* F */
  /* 0x47 */  REGULAR,
  /* 0x48 */  REGULAR,
  /* 0x49 */  REGULAR,
  /* 0x4a */  REGULAR,
  /* 0x4b */  REGULAR,
  /* 0x4c */  REGULAR,
  /* 0x4d */  REGULAR,
  /* 0x4e */  REGULAR,
  /* 0x4f */  REGULAR,
  /* 0x50 */  REGULAR,
  /* 0x51 */  REGULAR,
  /* 0x52 */  REGULAR,
  /* 0x53 */  REGULAR,
  /* 0x54 */  REGULAR,
  /* 0x55 */  REGULAR,
  /* 0x56 */  REGULAR,
  /* 0x57 */  REGULAR,
  /* 0x58 */  REGULAR,
  /* 0x59 */  REGULAR,
  /* 0x5a */  REGULAR,
  /* 0x5b */  DELIMITER,                  /* [ */
  /* 0x5c */  REGULAR,
  /* 0x5d */  DELIMITER,                  /* ] */
  /* 0x5e */  REGULAR,
  /* 0x5f */  REGULAR,
  /* 0x60 */  REGULAR,
  /* 0x61 */  REGULAR | HEXDIGIT,         /* a */
  /* 0x62 */  REGULAR | HEXDIGIT,         /* b */
  /* 0x63 */  REGULAR | HEXDIGIT,         /* c */
  /* 0x64 */  REGULAR | HEXDIGIT,         /* d */
  /* 0x65 */  REGULAR | HEXDIGIT,         /* e */
  /* 0x66 */  REGULAR | HEXDIGIT,         /* f */
  /* 0x67 */  REGULAR,
  /* 0x68 */  REGULAR,
  /* 0x69 */  REGULAR,
  /* 0x6a */  REGULAR,
  /* 0x6b */  REGULAR,
  /* 0x6c */  REGULAR,
  /* 0x6d */  REGULAR,
  /* 0x6e */  REGULAR,
  /* 0x6f */  REGULAR,
  /* 0x70 */  REGULAR,
  /* 0x71 */  REGULAR,
  /* 0x72 */  REGULAR,
  /* 0x73 */  REGULAR,
  /* 0x74 */  REGULAR,
  /* 0x75 */  REGULAR,
  /* 0x76 */  REGULAR,
  /* 0x77 */  REGULAR,
  /* 0x78 */  REGULAR,
  /* 0x79 */  REGULAR,
  /* 0x7a */  REGULAR,
  /* 0x7b */  DELIMITER,                  /* { */
  /* 0x7c */  REGULAR,
  /* 0x7d */  DELIMITER,                  /* } */
  /* 0x7e */  REGULAR,
  /* 0x7f */  REGULAR,
  /* 0x80 */  REGULAR,
  /* 0x81 */  REGULAR,
  /* 0x82 */  REGULAR,
  /* 0x83 */  REGULAR,
  /* 0x84 */  REGULAR,
  /* 0x85 */  REGULAR,
  /* 0x86 */  REGULAR,
  /* 0x87 */  REGULAR,
  /* 0x88 */  REGULAR,
  /* 0x89 */  REGULAR,
  /* 0x8a */  REGULAR,
  /* 0x8b */  REGULAR,
  /* 0x8c */  REGULAR,
  /* 0x8d */  REGULAR,
  /* 0x8e */  REGULAR,
  /* 0x8f */  REGULAR,
  /* 0x90 */  REGULAR,
  /* 0x91 */  REGULAR,
  /* 0x92 */  REGULAR,
  /* 0x93 */  REGULAR,
  /* 0x94 */  REGULAR,
  /* 0x95 */  REGULAR,
  /* 0x96 */  REGULAR,
  /* 0x97 */  REGULAR,
  /* 0x98 */  REGULAR,
  /* 0x99 */  REGULAR,
  /* 0x9a */  REGULAR,
  /* 0x9b */  REGULAR,
  /* 0x9c */  REGULAR,
  /* 0x9d */  REGULAR,
  /* 0x9e */  REGULAR,
  /* 0x9f */  REGULAR,
  /* 0xa0 */  REGULAR,
  /* 0xa1 */  REGULAR,
  /* 0xa2 */  REGULAR,
  /* 0xa3 */  REGULAR,
  /* 0xa4 */  REGULAR,
  /* 0xa5 */  REGULAR,
  /* 0xa6 */  REGULAR,
  /* 0xa7 */  REGULAR,
  /* 0xa8 */  REGULAR,
  /* 0xa9 */  REGULAR,
  /* 0xaa */  REGULAR,
  /* 0xab */  REGULAR,
  /* 0xac */  REGULAR,
  /* 0xad */  REGULAR,
  /* 0xae */  REGULAR,
  /* 0xaf */  REGULAR,
  /* 0xb0 */  REGULAR,
  /* 0xb1 */  REGULAR,
  /* 0xb2 */  REGULAR,
  /* 0xb3 */  REGULAR,
  /* 0xb4 */  REGULAR,
  /* 0xb5 */  REGULAR,
  /* 0xb6 */  REGULAR,
  /* 0xb7 */  REGULAR,
  /* 0xb8 */  REGULAR,
  /* 0xb9 */  REGULAR,
  /* 0xba */  REGULAR,
  /* 0xbb */  REGULAR,
  /* 0xbc */  REGULAR,
  /* 0xbd */  REGULAR,
  /* 0xbe */  REGULAR,
  /* 0xbf */  REGULAR,
  /* 0xc0 */  REGULAR,
  /* 0xc1 */  REGULAR,
  /* 0xc2 */  REGULAR,
  /* 0xc3 */  REGULAR,
  /* 0xc4 */  REGULAR,
  /* 0xc5 */  REGULAR,
  /* 0xc6 */  REGULAR,
  /* 0xc7 */  REGULAR,
  /* 0xc8 */  REGULAR,
  /* 0xc9 */  REGULAR,
  /* 0xca */  REGULAR,
  /* 0xcb */  REGULAR,
  /* 0xcc */  REGULAR,
  /* 0xcd */  REGULAR,
  /* 0xce */  REGULAR,
  /* 0xcf */  REGULAR,
  /* 0xd0 */  REGULAR,
  /* 0xd1 */  REGULAR,
  /* 0xd2 */  REGULAR,
  /* 0xd3 */  REGULAR,
  /* 0xd4 */  REGULAR,
  /* 0xd5 */  REGULAR,
  /* 0xd6 */  REGULAR,
  /* 0xd7 */  REGULAR,
  /* 0xd8 */  REGULAR,
  /* 0xd9 */  REGULAR,
  /* 0xda */  REGULAR,
  /* 0xdb */  REGULAR,
  /* 0xdc */  REGULAR,
  /* 0xdd */  REGULAR,
  /* 0xde */  REGULAR,
  /* 0xdf */  REGULAR,
  /* 0xe0 */  REGULAR,
  /* 0xe1 */  REGULAR,
  /* 0xe2 */  REGULAR,
  /* 0xe3 */  REGULAR,
  /* 0xe4 */  REGULAR,
  /* 0xe5 */  REGULAR,
  /* 0xe6 */  REGULAR,
  /* 0xe7 */  REGULAR,
  /* 0xe8 */  REGULAR,
  /* 0xe9 */  REGULAR,
  /* 0xea */  REGULAR,
  /* 0xeb */  REGULAR,
  /* 0xec */  REGULAR,
  /* 0xed */  REGULAR,
  /* 0xee */  REGULAR,
  /* 0xef */  REGULAR,
  /* 0xf0 */  REGULAR,
  /* 0xf1 */  REGULAR,
  /* 0xf2 */  REGULAR,
  /* 0xf3 */  REGULAR,
  /* 0xf4 */  REGULAR,
  /* 0xf5 */  REGULAR,
  /* 0xf6 */  REGULAR,
  /* 0xf7 */  REGULAR,
  /* 0xf8 */  REGULAR,
  /* 0xf9 */  REGULAR,
  /* 0xfa */  REGULAR,
  /* 0xfb */  REGULAR,
  /* 0xfc */  REGULAR,
  /* 0xfd */  REGULAR,
  /* 0xfe */  REGULAR,
  /* 0xff */  REGULAR,
};

/* Byte classification macros */
#define IS_REGULAR(c)         ((pdf_ctype[c] & REGULAR) != 0)
#define IS_DELIMITER(c)       ((pdf_ctype[c] & DELIMITER) != 0)
#define IS_WHITESPACE(c)      ((pdf_ctype[c] & WHITESPACE) != 0)
#define IS_DIGIT(c)           ((pdf_ctype[c] & DIGIT) != 0)
#define IS_EOL(c)             ((pdf_ctype[c] & EOL) != 0)
#define IS_HEXDIGIT(c)        ((pdf_ctype[c] & HEXDIGIT) != 0)

/* NUL character */
#define NUL                   (0x00)

/* EOL character byte values */
#define LF                    (0x0A)
#define CR                    (0x0D)

/* Convert hex digit to nibble value */
#define hex2value(h)          (IS_DIGIT(h) ? (h) - '0' : ((h) & ~0x20) - 'A' + 10)

/* Length for the small buffer */
#define SBUF_LENGTH           (64)

/* Size of state stack */
#define STATE_STACK_SIZE      (32)

/* PDF parsing state */
struct PDF_PARSE {
  int32         pdf_state[STATE_STACK_SIZE];/* Current PDF file structure state */
  int32         state_top;    /* Top of state stack index */

  uint8*        next;         /* Pointer to next char in buffer */
  uint8*        end;          /* Pointer to last char + 1 in buffer */

  int32         tok_state;    /* Current token parsing state stack */
  int32         token;        /* Last recognised PDF token */

  HqBool        str_escape;   /* Encountered an escaped character in a literal string */
  HqBool        str_octal;    /* Parsing octal character */
  int32         str_nesting;  /* Level of paren nesting */
  int32         str_octal_chars; /* Octal characters seen */

  HqBool        hex_char;     /* Encountered an escaped character */
  int32         hex_chars;    /* Hexadecimal characters seen */
  uint32        name_char;    /* Decoded hexadecimal character */

  HqBool        could_be_numeric; /* Could still be parsing a numeric token */
  int32         number_state; /* Numeric parsing state */
  int32         sign;         /* Sign of numeric < 0 => negative, > 0 => positive */
  int32         nleading;     /* Number of digits before a decimal point */
  int32         ntotal;       /* Total number of digits */
  int32         ileading;     /* Built up integer value */
  double        fleading;     /* Built up integer value when exceeds 32 bits (max of 53) */
  int32         i_numeric;    /* Returned integer numeric value */
  double        f_numeric;    /* Returned real numeric value */

  HqBool        seen_cr;      /* Seen a CR when parsing an EOL */
  HqBool        seen_eol;     /* Seen a EOL character */


  int32         dict_depth;   /* Dictionary nesting level */
  HqBool        length_seen;  /* Just seen Length name in 1st level dict */
  HqBool        prev_numeric; /* Previous token was a numeric */
  uint32        stream_len;   /* Value for Length in a stream dict */

  HqBool        buff_span;    /* endstream search spans parse buffers */

  uint8         sbuf[SBUF_LENGTH]; /* Used for checking stuff that could span buffers */
  uint8*        p_sbuf;       /* Pointer to next position to fill in the small buffer */

  HqBool        seen_eof;     /* Seen PDF EOF comment */
  Hq32x2        data_len;     /* Amount of PDF data seen */

  SPOOLER_IF    spooler_if;   /* Parsed data spooler callback interface */
};

/* PDF parsing states
 */
#define TOKEN_TYPE            (0)
#define TOKEN_ANY             (TOKEN_TYPE + 1)     /* Unrecognised token */
#define TOKEN_COMMENT         (TOKEN_TYPE + 2)     /* A comment */
#define TOKEN_PSTRING         (TOKEN_TYPE + 3)     /* A literal string */
#define TOKEN_HSTRING         (TOKEN_TYPE + 4)     /* A hex string */
#define TOKEN_NAME            (TOKEN_TYPE + 5)     /* A name */
#define TOKEN_TOKEN           (TOKEN_TYPE + 6)     /* A keyword */
#define TOKEN_DICT_START      (TOKEN_TYPE + 7)     /* A dict start */
#define TOKEN_DICT_END        (TOKEN_TYPE + 8)     /* A dict end */
#define TOKEN_ARRAY_START     (TOKEN_TYPE + 9)     /* An array start */
#define TOKEN_ARRAY_END       (TOKEN_TYPE + 10)    /* An array end */
#define TOKEN_PROC_START      (TOKEN_TYPE + 11)    /* A procedure start */
#define TOKEN_PROC_END        (TOKEN_TYPE + 12)    /* A procedure end */
#define TOKEN_INTEGER         (TOKEN_TYPE + 13)    /* An integer numeric */
#define TOKEN_REAL            (TOKEN_TYPE + 14)    /* A real numeric */

/* PDF token parsing states - use different numbers to token types to catch any
 * misuse between them.
 */
#define TOK_PARSE_ANY         (100)                 /* Looking for the next token to parse */
#define TOK_PARSE_COMMENT     (TOK_PARSE_ANY + 1)   /* Parsing a comment */
#define TOK_PARSE_PSTRING     (TOK_PARSE_ANY + 2)   /* Parsing a literal string */
#define TOK_PARSE_HSTRING     (TOK_PARSE_ANY + 3)   /* Parsing a hex string */
#define TOK_PARSE_DICT_START  (TOK_PARSE_ANY + 4)   /* Parsing a dict start */
#define TOK_PARSE_DICT_END    (TOK_PARSE_ANY + 5)   /* Parsing a dict end */
#define TOK_PARSE_NAME        (TOK_PARSE_ANY + 6)   /* Parsing a name */
#define TOK_PARSE_TOKEN       (TOK_PARSE_ANY + 7)   /* Parsing a keyword or number */

/* PDF file structure tracking states - use big numbers so new ones can be added
 * as needed within each logical area */
#define PDF_ERROR_STATE       (0)

#define PDF_HEADER            (10)

#define PDF_OBJECT            (20)
#define PDF_OBJECT_NUM        (PDF_OBJECT + 1)
#define PDF_OBJECT_GEN        (PDF_OBJECT + 2)
#define PDF_OBJECT_OBJ        (PDF_OBJECT + 3)
#define PDF_OBJECT_ANY        (PDF_OBJECT + 4)
#define PDF_OBJECT_ENDOBJ     (PDF_OBJECT + 5)

#define PDF_COMPOSITE         (30)
#define PDF_COMPOSITE_DICT    (PDF_COMPOSITE + 1)
#define PDF_COMPOSITE_ARRAY   (PDF_COMPOSITE + 2)
#define PDF_COMPOSITE_PROC    (PDF_COMPOSITE + 3)

#define PDF_STREAM            (40)
#define PDF_STREAM_START      (PDF_STREAM + 1)
#define PDF_STREAM_LEN        (PDF_STREAM + 2)
#define PDF_STREAM_SEARCH     (PDF_STREAM + 3)
#define PDF_STREAM_CHECK      (PDF_STREAM + 4)
#define PDF_STREAM_END        (PDF_STREAM + 5)

#define PDF_XREF              (50)

#define PDF_TRAILER           (60)
#define PDF_TRAILER_STARTXREF (PDF_TRAILER + 1)
#define PDF_TRAILER_EOF       (PDF_TRAILER + 2)

#define PDF_INCREMENTAL       (70)


#ifdef ASSERT_BUILD

/* Utility structures and functions to help debug the parser */

typedef struct PDF_STATES {
  uint32    state;          /* Parsing state */
  uint8*    str_state;      /* String version of state */
} PDF_STATES;

/* Initialise state string lookup table entry */
#define STATE_DECL(s) { (s), (uint8*)("" #s "") }

/* Simple array of parsing state to string version for tracing.
 * The array must be kept in ascending state value! */
static
PDF_STATES  pdf_states[] = {
  STATE_DECL(PDF_HEADER),

  STATE_DECL(PDF_OBJECT_NUM),
  STATE_DECL(PDF_OBJECT_GEN),
  STATE_DECL(PDF_OBJECT_OBJ),
  STATE_DECL(PDF_OBJECT_ANY),
  STATE_DECL(PDF_OBJECT_ENDOBJ),

  STATE_DECL(PDF_COMPOSITE_DICT),
  STATE_DECL(PDF_COMPOSITE_ARRAY),
  STATE_DECL(PDF_COMPOSITE_PROC),

  STATE_DECL(PDF_STREAM_START),
  STATE_DECL(PDF_STREAM_LEN),
  STATE_DECL(PDF_STREAM_SEARCH),
  STATE_DECL(PDF_STREAM_CHECK),
  STATE_DECL(PDF_STREAM_END),

  STATE_DECL(PDF_XREF),

  STATE_DECL(PDF_TRAILER),
  STATE_DECL(PDF_TRAILER_STARTXREF),
  STATE_DECL(PDF_TRAILER_EOF),

  STATE_DECL(PDF_INCREMENTAL)
};

/* Return string version of parse state. */
static
uint8* state_string(
  uint32  state)
{
  int32 i;

  /* Do an ordered search - quit when gone beyond where it should be */
  for ( i = 0; (i < NUM_ARRAY_ITEMS(pdf_states)) && (state < pdf_states[i].state); i++ ) {
    if ( pdf_states[i].state == state ) {
      return(pdf_states[i].str_state);
    }
  }

  return((uint8*)"*** Unknown parsing state ***");

} /* state_string */

/* Flag controlling trace of PDF parser state stack changes */
static HqBool state_trace = FALSE;

#endif /* ASSERT_BUILD */


/* Numeric token parsing states */
#define NUMERIC_SIGN          (1)
#define NUMERIC_INTEGER       (2)
#define NUMERIC_FRACTION      (3)


/* Is there input data still to be parsed */
#define MORE_DATA(p)          ((p)->next < (p)->end)

/* How much data left in the parse buffer */
#define PARSE_REMAINING(p)    (CAST_PTRDIFFT_TO_UINT32((p)->end - (p)->next))

/* Simple versions of getc()/putc() for the parse buffer */
#define PARSE_EOB             (-1)
#define PARSE_GETC(p)         (MORE_DATA(p) ? *(p)->next++ : PARSE_EOB)
#define PARSE_PUTC(p)         (--(p)->next)


/* The current PDF file structure state */
#define PARSE_STATE(p)        ((p)->pdf_state[(p)->state_top])


/* Initialise PDF structure parsing state stack with initial state */
static
void state_init(
  PDF_PARSE*  parse,
  int32       state)
{
  HQASSERT((parse != NULL),
           "state_init: NULL parser pointer");

  parse->state_top = 0;
  parse->pdf_state[parse->state_top] = state;

  HQTRACE(state_trace,
          ("Init - Depth: %2d  State: %s", parse->state_top, state_string(state)));

} /* state_init */


/* Push new current PDF structure parsing state.  i.e. transition to a new state
 * maintaining the current one to be returned to.
 * Returns FALSE if stack overflows. */
static
HqBool state_push(
  PDF_PARSE*  parse,
  int32       state)
{
  HQASSERT((parse != NULL),
           "state_push: NULL parse pointer");

  if ( ++parse->state_top == STATE_STACK_SIZE ) {
    /* Raise an alarm until we have had more experience of what problems there are */
    HQFAIL("state_push: stack overflow, please report");
    /* Remember to keep the stack index in range even in an error */
    parse->state_top--;
    return(FALSE);
  }
  HQTRACE(state_trace,
          ("Push - Depth: %2d  State: %s", parse->state_top, state_string(state)));
  parse->pdf_state[parse->state_top] = state;

  return(TRUE);

} /* state_push */


/* Pop the current PDF file structure state.  i.e. transition back up to the
 * original state at the higher level.
 * Returns FALSE if there was no higher state, else TRUE. */
static
HqBool state_pop(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "state_pop: NULL parse pointer");

  if ( --parse->state_top < 0 ) {
    /* Raise an alarm until we have had more experience of what problems there are */
    HQFAIL("state_pop: stack overflow, please report");
    /* Remember to keep the stack index in range even in an error */
    parse->state_top++;
    return(FALSE);
  }

  HQTRACE(state_trace,
          ("Pop  - Depth: %2d  State: %s", parse->state_top,
           state_string(parse->pdf_state[parse->state_top])));

#if defined(DEBUG_BUILD)
  /* Salt the last stack entry used */
  parse->pdf_state[parse->state_top + 1] = PDF_ERROR_STATE;
#endif /* DEBUG_BUILD */

  return(TRUE);

} /* state_pop */


/* Replace the current PDF file structure state.  i.e. transition to a new state
 * at the current level. */
static
void state_replace(
  PDF_PARSE*  parse,
  int32       state)
{
  HQASSERT((parse != NULL),
           "state_replace: NULL parse pointer");

  HQTRACE(state_trace,
          ("Repl -            State: %s (from %s)", state_string(state),
           state_string(parse->pdf_state[parse->state_top])));

  parse->pdf_state[parse->state_top] = state;

} /* state_replace */


/* Reset the small buffer to be empty */
static
void sbuf_reset(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "sbuf_reset: NULL parser state pointer");

  parse->p_sbuf = parse->sbuf;

} /* sbuf_reset */


/* Return the length of data in the small buffer */
static
uint32 sbuf_len(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "sbuf_len: NULL parser state pointer");
  HQASSERT((parse->p_sbuf >= parse->sbuf),
           "sbuf_len: small buffer pointer invalid");

  return(CAST_PTRDIFFT_TO_UINT32(parse->p_sbuf - parse->sbuf));

} /* sbuf_len*/


/* Return available space in the small buffer */
static
uint32 sbuf_space(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "sbuf_space: NULL parser state pointer");
  HQASSERT((parse->p_sbuf >= parse->sbuf),
           "sbuf_space: small buffer pointer invalid");

  return(CAST_PTRDIFFT_TO_UINT32(&parse->sbuf[SBUF_LENGTH] - parse->p_sbuf));

} /* sbuf_space */


/* Append a byte to the small buffer if there is space. */
static
void sbuf_add_byte(
  PDF_PARSE*  parse,
  int32       byte)
{
  HQASSERT((parse != NULL),
           "sbuf_add_byte: NULL parser state pointer");
  HQASSERT((parse->p_sbuf >= parse->sbuf),
           "sbuf_add_byte: small buffer pointer invalid");

  if ( parse->p_sbuf < &parse->sbuf[SBUF_LENGTH] ) {
    *parse->p_sbuf++ = CAST_SIGNED_TO_UINT8(byte);
  }

} /* sbuf_add_byte */


/* Append the small buffer with sequence of bytes.
 * Small buffer should not be filled from itself! */
static
void sbuf_add_bytes(
  PDF_PARSE*  parse,
  uint8*      bytes,
  uint32      len)
{
  uint32  space;

  HQASSERT((parse != NULL),
           "sbuf_add_bytes: NULL parse pointer");
  HQASSERT((bytes != NULL),
           "sbuf_add_bytes: NULL bytes pointer");
  HQASSERT((!((bytes >= parse->sbuf) && (bytes < &parse->sbuf[SBUF_LENGTH]))),
           "sbuf_add_bytes: filling small buffer from small buffer");

  space = sbuf_space(parse);
  if ( len > space ) {
    len = space;
  }
  HqMemCpy(parse->p_sbuf, bytes, len);
  parse->p_sbuf += len;

  HQASSERT((sbuf_len(parse) <= SBUF_LENGTH),
           "sbuf_add_bytes: small buffer overflow");

} /* sbuf_add_bytes */


/* Check if small buffer content matches given string */
static
HqBool sbuf_eq(
  PDF_PARSE*  parse,
  uint8*      string,
  uint32      length)
{
  HQASSERT((parse != NULL),
           "sbuf_eq: NULL parser pointer");
  HQASSERT((string != NULL),
           "sbuf_eq: NULL string pointer");
  HQASSERT((length <= SBUF_LENGTH),
           "sbuf_eq: string longer than small buffer");

  return((length == sbuf_len(parse)) &&
         (HqMemCmp(parse->sbuf, length, string, length)) == 0);

} /* sbuf_eq */


/* Check if small buffer content starts with given string. */
static
HqBool sbuf_startswith(
  PDF_PARSE*  parse,
  uint8*      string,
  uint32      length)
{
  HQASSERT((parse != NULL),
           "sbuf_startswith: NULL parser pointer");
  HQASSERT((string != NULL),
           "sbuf_startswith: NULL string pointer");
  HQASSERT((length <= SBUF_LENGTH),
           "sbuf_startswith: string longer small buffer");

  return((sbuf_len(parse) >= length) &&
         (HqMemCmp(parse->sbuf, length, string, length) == 0));

} /* sbuf_startswith */


/* Check if small buffer content starts with given string, and that any following character is whitespace */
static
HqBool sbuf_startwith_ws(
  PDF_PARSE*  parse,
  uint8*      string,
  uint32      length)
{
  HQASSERT((parse != NULL),
           "sbuf_startwith_ws: NULL parser pointer");
  HQASSERT((string != NULL),
           "sbuf_startwith_ws: NULL string pointer");
  HQASSERT((length <= SBUF_LENGTH),
           "sbuf_startwith_ws: string longer than small buffer");

  return((sbuf_len(parse) >= length) &&
         (HqMemCmp(parse->sbuf, length, string, length) == 0) &&
         ((sbuf_len(parse) == length) || IS_WHITESPACE(parse->sbuf[length])));

} /* sbuf_startwith_ws */


/* Create a new PDF parser instance */
PDF_PARSE* pdfparse_new(
  SPOOLER_IF* spooler_if)
{
  PDF_PARSE*  parse;

  HQASSERT((spooler_if != NULL),
           "pdfparse_new: NULL interface pointer");
  HQASSERT(((spooler_if->append != NULL) &&
            (spooler_if->pdflen != NULL) &&
            (spooler_if->alloc != NULL) &&
            (spooler_if->free != NULL)),
           "pdfparse_new: interface has NULL pointer");

  if ( (parse = (*spooler_if->alloc)(sizeof(PDF_PARSE))) != NULL ) {
    /* Initialise bare minimum needed prior to calling parser function */
    parse->spooler_if = *spooler_if;
    state_init(parse, PDF_HEADER);
    parse->tok_state = TOK_PARSE_ANY;
    parse->dict_depth = 0;
    parse->seen_eof = FALSE;
    Hq32x2FromInt32(&parse->data_len, 0);
  }

  return(parse);

} /* pdfparse_new */


/* Scan comment data up to and including any EOL.  Comment data excluding the
 * EOL are added to the small buffer.
 * Returns TRUE when the end of comment is seen, else FALSE to indicate more
 * data is needed to find the end of comment.
 */
static
HqBool parse_comment(
  PDF_PARSE*  parse)
{
  int32 byte;

  HQASSERT((parse != NULL),
           "parse_comment: NULL parser state pointer");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( IS_EOL(byte) || parse->seen_eol ) {
      if ( byte == LF ) {
        return(TRUE);
      }
      /* Must be a CR - only accept first in a sequence */
      if ( parse->seen_cr ) {
        PARSE_PUTC(parse);
        return(TRUE);
      }
      parse->seen_eol = TRUE;
      parse->seen_cr = TRUE;
      continue;
    }
    sbuf_add_byte(parse, byte);
  }

  return(FALSE);

} /* parse_comment */


/* Scan literal string until terminating matching closing parenthesis.  The
 * devilish detail is handling escaped and nested parentheses, octal sequences,
 * and the escaped backslashes.  No need to cache the string start data as not
 * needed.
 * Also, does not ignore \EOL sequences since we don't copy the string to the
 * small buffer and doesn't affect string parentheses parsing.
 * Returns TRUE when the end of the string is seen, else FALSE to indicate more
 * data is needed to find the end of the string.
 */
static
HqBool parse_pstring(
  PDF_PARSE*  parse)
{
  int32   byte;

  HQASSERT((parse != NULL),
           "parse_pstring: NULL parser state pointer");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    switch ( byte ) {
    case '\\':      /* Escaped character or octal */
      parse->str_escape = !parse->str_escape; /* Copes with \\ in a string */
      parse->str_octal = FALSE;
      break;

    case '(':       /* Open paren - see if we need to consider nesting */
      if ( !parse->str_escape ) {
        parse->str_nesting++;
      }
      parse->str_escape = FALSE;
      parse->str_octal = FALSE;
      break;

    case '0':       /* Octal digit if an escape seen */
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
      if ( parse->str_escape ) {
        parse->str_octal = TRUE;
        parse->str_octal_chars = 0;
        parse->str_escape = FALSE;
      }
      if ( parse->str_octal && (++parse->str_octal_chars == 3) ) {
        /* Limit of 3 octal digits for an octal character */
        parse->str_octal = FALSE;
      }
      break;

    case ')':       /* Closing paren - see if we need to consider nesting */
      if ( !parse->str_escape && (--parse->str_nesting == 0) ) {
        /* Found terminating paren */
        return(TRUE);
      }
      /* FALLTHROUGH */
    default:        /* Regular string character - reset special cases */
      parse->str_escape = FALSE;
      parse->str_octal = FALSE;
      break;
    }
  }

  /* Need more data */
  return(FALSE);

} /* parse_pstring */


/* Scan hex string until terminating > is seen.  Does not validate content of
 * string (i.e. that it is only hex characters and white space).
 * Returns TRUE when the end of the string is seen, else FALSE to indicate more
 * data is needed to find the end of the string.
 */
static
HqBool parse_hstring(
  PDF_PARSE*  parse)
{
  int32   byte;

  HQASSERT((parse != NULL),
           "parse_hstring: NULL parse state pointer");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( byte == '>' ) {
      return(TRUE);
    }
  }

  /* Need more data */
  return(FALSE);

} /* parse_hstring */


/* Parse start of dict token - basically look for a following <.  If there isn't
 * one then must be start of a hex string.
 * Returns TRUE when seen next character and decided if it was start dict token,
 * or the start of a hex string, else FALSE to indicate more data is needed to
 * find the end of the string.
 */
static
HqBool parse_dict_start(
  PDF_PARSE*  parse,
  HqBool*     hstring)
{
  int32   byte;

  HQASSERT((parse != NULL),
           "parse_dict_start: NULL parser state pointer");
  HQASSERT((hstring != NULL),
           "parse_dict_start: NULL pointer to returned hex string flag");

  *hstring = FALSE;

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( byte != '<' ) {
      /* Not a dict, so treat as a hex string, back up the buffer pointer */
      PARSE_PUTC(parse);
      *hstring = TRUE;
    }
    /* Yep, twas a dict start token */
    return(TRUE);
  }

  /* Need more data */
  return(FALSE);

} /* parse_dict_start */


/* Parse end of dict token - it expects the next char is a >.  If it isn't we
 * just back up one character and restart parsing.
 * Returns TRUE unless it needs more data when it returns FALSE.
 */
static
HqBool parse_dict_end(
  PDF_PARSE*    parse)
{
  int32   byte;

  HQASSERT((parse != NULL),
           "parse_dict_end: NULL parse state pointer");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( byte != '>' ) {
      PARSE_PUTC(parse);
    }
    return(TRUE);
  }

  /* Need more data */
  return(FALSE);

} /* parse_dict_end */


#define PDF_INT32_MAX     (0x7fffffff)
#define PDF_INT32_MIN     (-PDF_INT32_MAX - 1)
#define PDF_UINT32_MAX    (0xffffffffu)

/* Scan a PDF name up to the next non-regular character.  Handling pound escaped
 * character values makes it more complex.  Pound escape is recursive, that is
 * if the decoded character is the pound sign then that starts a new escape
 * sequence - seen in the wild!  Also, how to hanle #0000 as NULs are not
 * allowed to be escaped.
 * Return TRUE if the end of the name has been encountered, or FALSE if it needs
 * more data.
 */
static
HqBool parse_name(
  PDF_PARSE*    parse)
{
  int32   byte;
  int32   digit;

  HQASSERT((parse != NULL),
           "parse_name: NULL parse state pointer.");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( !IS_REGULAR(byte) ) {
      PARSE_PUTC(parse);
      /* Found a terminating character for the name. */
      if ( parse->could_be_numeric && (parse->ntotal > 0) ) {
        /* Looks like the token was a number */
        if ( parse->number_state == NUMERIC_INTEGER ) {
          /* Integer only, didn't even see a decimal point */
          if ( parse->ntotal < 10 ) {
            /* Definitely integer ranged */
            parse->i_numeric = (parse->sign > 0) ? parse->ileading : -parse->ileading;
            parse->token = TOKEN_INTEGER;
          } else if ( (parse->fleading >= PDF_INT32_MIN) &&
                      (parse->fleading <= PDF_INT32_MAX) ) {
            /* Double value that fits in a integer */
            parse->i_numeric = (int32)((parse->sign > 0) ? parse->fleading : -parse->fleading);
            parse->token = TOKEN_INTEGER;
          } else { /* Too big for an integer */
            parse->f_numeric = parse->fleading;
            parse->token = TOKEN_REAL;
          }

        } else { /* Real number */
          if ( parse->ntotal < 10 ) {
            parse->fleading = (double)parse->ileading;
          }
          if ( parse->sign < 0 ) {
            parse->fleading = -parse->fleading;
          }
          parse->f_numeric = parse->fleading/pow(10.0, (double)(parse->ntotal - parse->nleading));
          parse->token = TOKEN_REAL;
        }

      } else { /* Plain ol' token */
        parse->token = TOKEN_TOKEN;
        /* Add any pending escaped character or the literal # */
        if ( parse->hex_char ) {
          if ( parse->hex_chars > 0 ) {
            byte = (uint8)parse->name_char;
          } else { /* # with no hex digits - add the literal # */
            byte = '#';
          }
          sbuf_add_byte(parse, byte);
        }
      }
      return(TRUE);
    }

    if ( parse->hex_char ) {
      /* If processing an escaped character build up the hex digits. */
      if ( IS_HEXDIGIT(byte) ) {
        parse->name_char = (parse->name_char << 4) | hex2value(byte);
        if ( ++parse->hex_chars == 1 ) {
          /* Seen one hex char, look for a second */
          continue;
        }
      }
      /* Either not a hex digit or seen two hex digits */
      if ( parse->hex_chars > 0 ) {
        /* Use the decoded character value - could only be a single hex char */
        if ( parse->name_char == '#' ) {
          /* An escaped hex escape - treat as start of a new hex escape */
          parse->hex_chars = 0;
          parse->name_char = 0;
          continue;
        }
        if ( parse->name_char == NUL ) {
          /* NULs cannot be escaped, don't add to the small buffer but treat as
           * a white space delimiter */
          if ( !IS_HEXDIGIT(byte) ) {
            /* Only saw one hex digit - push back character just read */
            PARSE_PUTC(parse);
          }
          parse->token = TOKEN_TOKEN;
          return(TRUE);
        }
        /* Add decoded character */
        sbuf_add_byte(parse, parse->name_char);

      } else { /* No hex digits - add a literal # */
        sbuf_add_byte(parse, '#');
      }
      /* Finished processing a pound sign */
      parse->hex_char = FALSE;

      if ( IS_HEXDIGIT(byte) ) {
        /* Byte was second hex char so back for next char in buffer */
        continue;
      }

    } else if ( byte == '#' ) {
      /* Got an escaped hex byte value - token can no longer be a numeric */
      parse->hex_char = TRUE;
      parse->hex_chars = 0;
      parse->name_char = 0;
      parse->could_be_numeric = FALSE;
      continue;

    } else if ( parse->could_be_numeric ) {
      /* Still could be a numeric, parse a value */
      switch ( parse->number_state ) {
      case NUMERIC_SIGN:
        /* Possible leading sign */
        parse->sign = 1;
        parse->ileading = 0;
        parse->ntotal = 0;
        parse->number_state = NUMERIC_INTEGER;
        if ( byte == '+' ) {
          break;
        }
        if ( byte == '-' ) {
          parse->sign = -1;
          break;
        }
        /* FALLTHROUGH */

      case NUMERIC_INTEGER:
        if ( IS_DIGIT(byte) ) {
          parse->ntotal++;
          digit = byte - '0';
          if ( parse->ntotal < 10 ) {
            parse->ileading = parse->ileading*10 + digit;
          } else if ( parse->ntotal > 10 ) {
            parse->fleading = parse->fleading*10 + digit;
          } else { /* Overflowing an int32, use double */
            parse->fleading = (double)parse->ileading*10 + digit;
          }
          break;
        }
        if ( byte == '.' ) {
          parse->nleading = parse->ntotal;
          parse->number_state = NUMERIC_FRACTION;
        } else {
          /* Not a number after all */
          parse->could_be_numeric = FALSE;
        }
        break;

      case NUMERIC_FRACTION:
        if ( IS_DIGIT(byte) ) {
          parse->ntotal++;
          digit = byte - '0';
          if ( parse->ntotal < 10 ) {
            parse->ileading = parse->ileading*10 + digit;
          } else if ( parse->ntotal > 10 ) {
            parse->fleading = parse->fleading*10 + digit;
          } else {
            parse->fleading = (double)parse->ileading*10 + digit;
          }
        } else {
          /* Not a number */
          parse->could_be_numeric = FALSE;
        }
        break;
      }
    }

    /* Add original byte to the small buffer */
    sbuf_add_byte(parse, byte);
  }

  /* Need more data */
  return(FALSE);

} /* parse_name */


/* Consume a PDF EOL, comprising CR+LF or just LF.  CR by itself is not an EOL.
 * Does not skip over any leading whitespace.
 *
 * THIS SHOULD ONLY BE CALLED WHEN PARSING THE EOL AFTER stream KEYWORD TO FIND
 * THE START OF THE STREAM DATA.
 *
 * Returns TRUE if it needs more data to parse, FALSE when it decides if there
 * was an EOL or not. */
static
HqBool parse_eol(
  PDF_PARSE*  parse)
{
  int32   byte;

  HQASSERT((parse != NULL),
           "parse_eol: NULL parser pointer");
  HQASSERT((PARSE_STATE(parse) == PDF_STREAM_START),
           "parse_eol: called when not processing start of a stream");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( byte == LF ) {
      return(TRUE);
    }
    if ( parse->seen_cr ) {
      /* CR should be followed by LF, if not assume 2 bytes of stream data */
      parse->stream_len -= 2;
      return(TRUE);
    }
    parse->seen_cr = TRUE;
  }

  return(FALSE);

} /* parse_eol */


/* Skip over white space characters in the buffer.  Note, comments are not
 * treated as white space - could be with an extra flag.
 * Returns TRUE when finds start of new token, else FALSE to indicate the end of
 * the buffer has been reached with no new token.
 */
static
HqBool skip_ws(
  PDF_PARSE*  parse)
{
  int32 byte;

  HQASSERT((parse != NULL),
           "skip_ws: NULL parser pointer");

  while ( (byte = PARSE_GETC(parse)) != PARSE_EOB ) {
    if ( !IS_WHITESPACE(byte) ) {
      PARSE_PUTC(parse);
      return(TRUE);
    }
  }

  return(FALSE);

} /* skip_ws */


/* Consume stream data based on detected Length value.
 * Return FALSE if more parse data is needed, else TRUE to indicate the end of
 * the stream data has been reached.
 */
static
HqBool consume_stream_len(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "consume_stream_len: NULL parser pointer");

  if ( PARSE_REMAINING(parse) <= parse->stream_len ) {
    /* Consume remainder of current buffer and get more data */
    parse->stream_len -= PARSE_REMAINING(parse);
    return(FALSE);
  }

  /* Advance buffer pointer to end of stream data */
  parse->next += parse->stream_len;
  return(TRUE);

} /* consume_stream_len */


/* This is a pre-calculated bad character skip array for the
 * Boyer-Moore-Horspool algorithm for a search pattern of endstream.  The skip
 * values for endstream are:
 *
 *  e n d s t r e a m
 *  2 7 6 5 4 3 2 1 0
 */
static
uint8 bmh_skip[256] = {
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 1, 9, 9, 6, 2, 9, 9, 9, 9, 9, 9, 9, 0, 7, 9,
  9, 9, 3, 5, 4, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9
};


/* Search the parse buffer for the next occurrence of endstream.  The parse
 * buffer pointer points to the first byte after endstream.
 * Search is a modified Boyer-Moore-Horspool search with pre-calculated bad
 * character skip table for the search string endstream.  The modification is
 * that the buffer pointer points immediately after the string being searched
 * for.
 * Returns FALSE if needs more date to find endstream, else TRUE.
 */
static
HqBool consume_stream_search(
  PDF_PARSE*  parse)
{
  uint32  skip;
  uint8*  p_start;
  uint8*  p_pat;
  uint8*  p_data;
#define STREAM_PAT      ("endstream")
#define STREAM_PAT_LEN  (sizeof(STREAM_PAT) - 1)
  static uint8* endstream = (uint8*)STREAM_PAT;

  if ( parse->buff_span ) {
    /* If there is not enough data in the sbuf and the current parser buffer to
     * do a match then append what we have to sbuf and get more data */
    skip = STREAM_PAT_LEN - sbuf_len(parse);
    if ( PARSE_REMAINING(parse) < skip ) {
      sbuf_add_bytes(parse, parse->next, PARSE_REMAINING(parse));
      return(FALSE);
    }

    /* Custom version of BMH algorithm to cope with case where search spans sbuf
     * and parse buffer. Since can only happen with a new parse buffer next must
     * point to the start of the buffer data */
    p_start = parse->next;
    while ( (parse->next + skip - p_start) < STREAM_PAT_LEN ) {
      parse->next += skip;
      p_data = parse->next;
      p_pat = endstream + STREAM_PAT_LEN;
      while ( *(--p_pat) == *p_data ) {
        if ( p_pat == endstream ) {
          /* Found a candidate match, parse buffer points to last character of
           * endstream so nudge it up one more byte */
          parse->next++;
          return(TRUE);
        }
        /* Handle backing up beyond start of parse buffer */
        if ( p_data == p_start ) {
          p_data = parse->p_sbuf - 1;
        } else {
          p_data--;
        }
      }
      skip = max(CAST_PTRDIFFT_TO_UINT32(endstream + STREAM_PAT_LEN - p_pat),
                 bmh_skip[*p_data]);
    }
    /* No longer tracking spanning parse buffers.  This is important in case an
     * endstream match is not a valid match and we restart searching. */
    parse->buff_span = FALSE;

  } else {
    /* Usual case of matching endstream in the parse buffer */
    skip = STREAM_PAT_LEN;
  }

  /* Classic BMH algorithm with a couple of tweaks.  It searches up to the point
   * where the skip would be beyond the end of the parse buffer, in which case
   * it stops and the remainder is copied to sbuf for handling in code above.
   * Plus on matching endstream, the parse buffer pointer points to the first
   * byte after it. */
  while ( PARSE_REMAINING(parse) >= skip ) {
    parse->next += skip;
    p_pat = endstream + STREAM_PAT_LEN;
    while ( *(--p_pat) == *parse->next ) {
      if ( p_pat == endstream ) {
        /* Found a candidate match, parse buffer points to first character of
         * endstream, so advance over it */
        parse->next += STREAM_PAT_LEN;
        return(TRUE);
      }
      parse->next--;
    }
    skip = max(CAST_PTRDIFFT_TO_UINT32(endstream + STREAM_PAT_LEN - p_pat),
               bmh_skip[*parse->next]);
  }
  sbuf_reset(parse);
  sbuf_add_bytes(parse, parse->next, PARSE_REMAINING(parse));
  parse->buff_span = TRUE;
  return(FALSE);

} /* consume_stream_search */


/* Parse the next PDF token.  White space is automatically skipped - i.e. never
 * returned as a token type.
 * parse->token is set for the new token type recognised, parse->tok_state
 * is reset to TOK_PARSE_ANY when the end of the token is seen.  Comments,
 * names, numerics (as text), and tokens are returned (possibly truncated) in
 * the small buffer.
 * Returns TRUE if finished parsing a token, else FALSE to indicate that more
 * data is needed.
 */
static
int32 parse_token(
  PDF_PARSE*  parse)
{
  int32   byte;
  HqBool  hstring;

  HQASSERT((parse != NULL),
           "parse_token: NULL parse state pointer");

  for (;;) {
    switch ( parse->tok_state ) {
    case TOK_PARSE_ANY:
      /* Find start of next token and reset small buffer */
      if ( !skip_ws(parse) ) {
        return(FALSE);
      }
      sbuf_reset(parse);

      /* Detect next token type */
      switch ( byte = PARSE_GETC(parse) ) {
      case '%':
        parse->seen_eol = FALSE;
        parse->seen_cr = FALSE;
        parse->tok_state = TOK_PARSE_COMMENT;
        break;
      case '(':
        parse->str_escape = FALSE;
        parse->str_octal = FALSE;
        parse->str_nesting = 1;
        parse->tok_state = TOK_PARSE_PSTRING;
        break;
      case '/':
        parse->could_be_numeric = FALSE;
        parse->hex_char = FALSE;
        parse->tok_state = TOK_PARSE_NAME;
        break;
      case '<':
        parse->tok_state = TOK_PARSE_DICT_START;
        break;
      case '>':
        parse->tok_state = TOK_PARSE_DICT_END;
        break;
      case '[':
        parse->token = TOKEN_ARRAY_START;
        return(TRUE);
      case ']':
        parse->token = TOKEN_ARRAY_END;
        return(TRUE);
      case '{':
        parse->token = TOKEN_PROC_START;
        return(TRUE);
      case '}':
        parse->token = TOKEN_PROC_END;
        return(TRUE);
      default:
        HQASSERT((IS_REGULAR(byte)),
                 "parse_token: not a regular character");
        /* Have to back up one character since the recognised character is
         * significant, e.g. if it is a # sign. */
        PARSE_PUTC(parse);
        parse->could_be_numeric = TRUE;
        parse->number_state = NUMERIC_SIGN;
        parse->ntotal = 0;
        parse->hex_char = FALSE;
        parse->tok_state = TOK_PARSE_TOKEN;
        break;

      case PARSE_EOB:
        HQFAIL("parse_token: reached end of buffer - should never happen");
        return(FALSE);
      }
      break;

    case TOK_PARSE_COMMENT:
      if ( !parse_comment(parse) ) {
        return(FALSE);
      }
      parse->token = TOKEN_COMMENT;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_PSTRING:
      if ( !parse_pstring(parse) ) {
        return(FALSE);
      }
      parse->token = TOKEN_PSTRING;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_HSTRING:
      if ( !parse_hstring(parse) ) {
        return(FALSE);
      }
      parse->token = TOKEN_HSTRING;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_DICT_START:
      /* Could be start of hex string so need to check */
      if ( !parse_dict_start(parse, &hstring) ) {
        return(FALSE);
      }
      if ( hstring ) {
        parse->tok_state = TOK_PARSE_HSTRING;
        break;
      }
      parse->token = TOKEN_DICT_START;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_DICT_END:
      if ( !parse_dict_end(parse) ) {
        return(FALSE);
      }
      parse->token = TOKEN_DICT_END;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_NAME:
      if ( !parse_name(parse) ) {
        return(FALSE);
      }
      parse->token = TOKEN_NAME;
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    case TOK_PARSE_TOKEN:
      if ( !parse_name(parse) ) {
        return(FALSE);
      }
      /* Token type set in parse_name */
      parse->tok_state = TOK_PARSE_ANY;
      return(TRUE);

    default:
      HQFAIL("parse_token: internal invalid token parsing state");
      parse->tok_state = TOK_PARSE_ANY;
      parse->token = TOKEN_ANY;
      return(TRUE);
    }
  }

  /* To check that there are no paths to the end of this function enable the
   * following code to be compiled - you should get a warning about unreachable
   * code - there is a problem if you don't */
#if 0
  /* NEVERREACHED */
  return(FALSE);
#endif

} /* parse_token */


/* Start or continue parsing the next PDF token skipping all white space and
 * comments. */
static
HqBool next_token(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "next_token: NULL parse state pointer");

  do {
    if ( !parse_token(parse) ) {
      return(FALSE);
    }
  } while ( parse->token == TOKEN_COMMENT );

  return(TRUE);

} /* next_token */


/* Check if the small buffer contains what looks like a PDF header.
 * Remember, the parser strips the leading % from comments when filling in the
 * small buffer.
 */
static
HqBool is_pdf_header(
  PDF_PARSE*  parse)
{
  HqBool        seen_digit;
  uint8*        p_comment;
  uint8*        p_pdf;
  static uint8* pdf = (uint8*)"PDF-";

  HQASSERT((parse != NULL),
           "is_pdf_header: NULL parser pointer");
  HQASSERT((parse->token == TOKEN_COMMENT),
           "is_pdf_header: current token is not a comment");

  p_comment = parse->sbuf;

  /* Match the leading PDF- */
  p_pdf = pdf;
  do {
    if ( (p_comment == parse->p_sbuf) ||
         (*p_comment++ != *p_pdf++) ) {
      return(FALSE);
    }
  } while ( *p_pdf );

  /* Match 1 or more digits */
  seen_digit = FALSE;
  while ( (p_comment < parse->p_sbuf) && IS_DIGIT(*p_comment) ) {
    seen_digit = TRUE;
    p_comment++;
  }
  if ( !seen_digit ) {
    return(FALSE);
  }

  /* Match dot decimal separator if not reached the end of the comment */
  if ( (p_comment == parse->p_sbuf) || (*p_comment++ != '.') ) {
    return(FALSE);
  }

  /* Match 1 or more digits if not reached the end of the comment */
  seen_digit = FALSE;
  while ( (p_comment < parse->p_sbuf) && IS_DIGIT(*p_comment) ) {
    seen_digit = TRUE;
    p_comment++;
  }

  if ( !seen_digit ) {
    return(FALSE);
  }

  /* Strictly there can be no white space after the fractional part but we'll be
   * generous. Note: there will be no EOL characters as they are not included by
   * parse_comment so only SP, HT, FF, and NUL */
  while ( p_comment < parse->p_sbuf ) {
    if ( !IS_WHITESPACE(*p_comment) ) {
      return(FALSE);
    }
    p_comment++;
  }

  return(TRUE);

} /* is_pdf_header */


/* Update parser state and spooler having seen an %%EOF comment where it makes
 * sense.
 */
static
void do_eof(
  PDF_PARSE*  parse)
{
  Hq32x2  pdf_len;

  HQASSERT((parse != NULL),
           "do_eof: NULL parser pointer");

  parse->seen_eof = TRUE;
  /* Update length of PDF data in spooled data */
  Hq32x2SubtractUint32(&pdf_len, &parse->data_len, PARSE_REMAINING(parse));
  (*parse->spooler_if.pdflen)(parse->spooler_if.private_data, &pdf_len);

} /* do_eof */


/* Continue parsing the supplied buffer looking for the end of the job.
 * Returns PDFPARSE_MORE_DATA if more data is needed to identify the end of the
 * job,
 * PDFPARSE_SPOOL_ERROR if there was an error spooling the given job data,
 * PDFPARSE_NOT_PDF if the first lot of data does not look like a PDF job,
 * PDFPARSE_EOJ if the end of the PDF job was found in the data.
 * PDFPARSE_PARSE_ERROR if the parser hits an internal parsing error.
 */
int32 pdfparse(
  PDF_PARSE*  parse,
  uint8*      buffer,
  int32       length)
{
  HQASSERT((parse != NULL),
           "pdfparse: NULL parser pointer");
  HQASSERT((buffer != NULL),
           "pdfparse: NULL buffer pointer");
  HQASSERT((length > 0),
           "pdfparse: invalid buffer length");

  /* Catch invalid buffer lengths */
  if ( length <= 0 ) {
    return(PDFPARSE_MORE_DATA);
  }

  /* Write buffer to the spool device - if this doesn't work no point parsing
   * the buffer. */
  if ( !(*parse->spooler_if.append)(parse->spooler_if.private_data, buffer, length) ) {
    return(PDFPARSE_SPOOL_ERROR);
  }
  Hq32x2AddUint32(&parse->data_len, &parse->data_len, length);

  /* Set up parsing pointers for new buffer */
  parse->next = buffer;
  parse->end = buffer + length;

  for (;;) {
    switch ( PARSE_STATE(parse) ) {
    case PDF_HEADER:
      /* On first pass through check buffer starts with comment leader -
       * subsequent passes (if needed when given a very small input buffer) will
       * have the token state set to comment */
      if ( !parse_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      /* Must start with something that looks like a PDF header comment */
      if ( (parse->token != TOKEN_COMMENT) || !is_pdf_header(parse) ) {
        return(PDFPARSE_NOT_PDF);
      }
      /* Valid PDF header, next up is the body */
      state_replace(parse, PDF_OBJECT_NUM);
      /* FALLTHROUGH */

    case PDF_OBJECT_NUM:
      /* Look for start of a new object definition, an object number, but could
       * be start of the cross-reference table or the end of a trailer when a
       * cross reference stream is used. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_INTEGER:
      case TOKEN_REAL:
        /* Got the object number */
        state_replace(parse, PDF_OBJECT_GEN);
        break;
      case TOKEN_TOKEN:
        /* Looks like a keyword, check for possible matches */
        if ( sbuf_eq(parse, STRING_AND_LENGTH("xref")) ) {
          /* Found the cross-reference table */
          state_replace(parse, PDF_XREF);

        } else if ( sbuf_eq(parse, STRING_AND_LENGTH("startxref")) ) {
          /* Using a cross-reference stream - goto last part of trailer */
          state_replace(parse, PDF_TRAILER_EOF);
        }
        break;
      default:
        /** \todo - error recovery */
        break;
      }
      break;

    case PDF_OBJECT_GEN:
      /* Look for object definition generation number.  Will only get here after
       * an object number. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_INTEGER:
      case TOKEN_REAL:
        /* Got the object generation number */
        state_replace(parse, PDF_OBJECT_OBJ);
        break;
      default:
        /** \todo - error recovery */
        break;
      }
      break;

    case PDF_OBJECT_OBJ:
      /* Look for keyword introducing the object.  Will only get here after an
       * object number and generation. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("obj")) ) {
          /* Got an object definition */
          state_replace(parse, PDF_OBJECT_ENDOBJ);
          if ( !state_push(parse, PDF_OBJECT_ANY) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
        }
        break;
      default:
        /** \todo - error recovery */
        break;
      }
      break;

    case PDF_OBJECT_ANY:
      /* Look for PDF object - either body of object definition, or content of a
       * dict, array, or procedure.  This tracks nesting of composite objects.
       * Ignores names and numerics and most tokens, but looks for some
       * keywords to mark the end of a object definition or the start of stream
       * data. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_DICT_START:
        HQASSERT((parse->dict_depth == 0),
                 "pdfparse: dict depth not zero at start of object definition");
        parse->dict_depth = 1;
        parse->stream_len = 0;
        parse->prev_numeric = FALSE;
        parse->length_seen = FALSE;
        if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_ARRAY_START:
        if ( !state_push(parse, PDF_COMPOSITE_ARRAY) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_PROC_START:
        if ( !state_push(parse, PDF_COMPOSITE_PROC) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("endobj")) ) {
          if ( !state_pop(parse) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
          state_replace(parse, PDF_OBJECT_NUM);

        } else if ( sbuf_eq(parse, STRING_AND_LENGTH("stream")) ) {
          if ( parse->stream_len > 0 ) {
            parse->seen_cr = FALSE;
            state_replace(parse, PDF_STREAM_START);
          } else {
            parse->buff_span = FALSE;
            state_replace(parse, PDF_STREAM_SEARCH);
          }

        } else if ( sbuf_eq(parse, STRING_AND_LENGTH("endobjxref")) ) {
          /* Seen one problem file with this in */
          if ( !state_pop(parse) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
          state_replace(parse, PDF_XREF);

        } else if ( sbuf_eq(parse, STRING_AND_LENGTH("trailer")) ) {
          if ( !state_pop(parse) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
          state_replace(parse, PDF_TRAILER);
        }
        break;
      }
      break;

    case PDF_COMPOSITE_DICT:
      /* Handle dictionary content.  Track nested composite objects.
       * Want to look for Length entry in outermost dict in case a stream
       * object. Fun is handling object references. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_DICT_START:
        parse->prev_numeric = FALSE;
        parse->length_seen = FALSE;
        parse->dict_depth += 1;
        if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_ARRAY_START:
        parse->prev_numeric = FALSE;
        parse->length_seen = FALSE;
        if ( !state_push(parse, PDF_COMPOSITE_ARRAY) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_PROC_START:
        parse->prev_numeric = FALSE;
        parse->length_seen = FALSE;
        if ( !state_push(parse, PDF_COMPOSITE_PROC) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_DICT_END:
        parse->prev_numeric = FALSE;
        parse->length_seen = FALSE;
        /* Reached the end of the dictionary */
        parse->dict_depth -= 1;
        if ( !state_pop(parse) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_NAME:
        if ( parse->dict_depth == 1 ) {
          if ( !parse->length_seen ) {
            parse->length_seen = sbuf_eq(parse, STRING_AND_LENGTH("Length"));
          } else {
            parse->length_seen = FALSE;
          }
        }
        parse->prev_numeric = FALSE;
        break;
      case TOKEN_INTEGER:
      case TOKEN_REAL:
        if ( parse->dict_depth == 1 ) {
          if ( parse->length_seen ) {
            /* Only accept values greater than 0. */
            parse->stream_len = 0;
            if ( parse->token == TOKEN_INTEGER ) {
              if ( parse->i_numeric > 0 ) {
                parse->stream_len = parse->i_numeric;
              }
            } else if ( (parse->f_numeric > 0) &&
                        (parse->f_numeric <= PDF_UINT32_MAX) ) {
              /* Use stream length if it can be held in a uint32 */
              parse->stream_len = (uint32)parse->f_numeric;
            }
            parse->prev_numeric = TRUE;
          } else if ( parse->prev_numeric ) {
            /* Seen two numerics in a row - looks like a object reference */
            parse->stream_len = 0;
            parse->prev_numeric = FALSE;
          }
        }
        parse->length_seen = FALSE;
        break;
      default:
        parse->length_seen = FALSE;
        parse->prev_numeric = FALSE;
        break;
      }
      break;

    case PDF_COMPOSITE_ARRAY:
      /* Handle array content. Track nested composite objects. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_DICT_START:
        parse->dict_depth += 1;
        if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_ARRAY_START:
        if ( !state_push(parse, PDF_COMPOSITE_ARRAY) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_PROC_START:
        if ( !state_push(parse, PDF_COMPOSITE_PROC) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_ARRAY_END:
        if ( !state_pop(parse) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      }
      break;

    case PDF_COMPOSITE_PROC:
      /* Handle procedure content. Track nested composite objects. */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_DICT_START:
        parse->dict_depth += 1;
        if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_ARRAY_START:
        if ( !state_push(parse, PDF_COMPOSITE_ARRAY) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_PROC_START:
        if ( !state_push(parse, PDF_COMPOSITE_PROC) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      case TOKEN_PROC_END:
        if ( !state_pop(parse) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;
      }
      break;

    case PDF_STREAM_START:
      /* Skip the PDF EOL to align with the start of the stream data */
      if ( !parse_eol(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      state_replace(parse, PDF_STREAM_LEN);
      /* FALLTHROUGH */

    case PDF_STREAM_LEN:
      /* Consumer length bytes */
      if ( !consume_stream_len(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      state_replace(parse, PDF_STREAM_END);
      /* FALLTHROUGH */

    case PDF_STREAM_END:
      /* Look for endstream keyword */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("endstream")) ) {
          if ( !state_pop(parse) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
          break;
        }
      default:
        /** \todo - error recovery */
        break;
      }
      break;

    case PDF_STREAM_SEARCH:
      /* Straight search for endstream keyword */
      if ( !consume_stream_search(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      state_replace(parse, PDF_STREAM_CHECK);
      /* FALLTHROUGH */

    case PDF_STREAM_CHECK:
      /* Catch endstream right at the end of the parse buffer */
      if ( !MORE_DATA(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      /* endstream should be followed by white space prior to the endobj.
       * Could make it white space or delimiter. */
      if ( !IS_WHITESPACE(*parse->next) ) {
        state_replace(parse, PDF_STREAM_SEARCH);
      } else {
        if ( !state_pop(parse) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
      }
      break;

    case PDF_OBJECT_ENDOBJ:
      /* Look for endobj keyword */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("endobj")) ) {
          state_replace(parse, PDF_OBJECT_NUM);
          break;
        }
      default:
        /** \todo - error recovery */
        break;
      }
      break;

    case PDF_XREF:
      /* Skip over the xref table sections until we hit the trailer */
      if ( !parse_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("trailer")) ) {
          /* Found start of the trailer */
          state_replace(parse, PDF_TRAILER);
        }
        break;
      default:
        break;
      }
      break;

    case PDF_TRAILER:
      /* trailer is usually followed by a dict */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_DICT_START:
        /* Consume the trailer dict */
        HQASSERT((parse->dict_depth == 0),
                 "pdfparse: dict depth not zero in trailer");
        parse->dict_depth = 1;
        state_replace(parse, PDF_TRAILER_STARTXREF);
        if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
          return(PDFPARSE_PARSE_ERROR);
        }
        break;

      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("startxref")) ) {
          /* No dict - go straight to looking for an %%EOF */
          state_replace(parse, PDF_TRAILER_EOF);
        }
        break;
      }
      break;

    case PDF_TRAILER_STARTXREF:
      /* Look for startxref after the trailer dict */
      if ( !next_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      if ( (parse->token == TOKEN_TOKEN) &&
           sbuf_eq(parse, STRING_AND_LENGTH("startxref")) ) {
        state_replace(parse, PDF_TRAILER_EOF);
      }
      break;

    case PDF_TRAILER_EOF:
      /* Look for the %%EOF comment - ignores the xref offset as not used */
      if ( !parse_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      /* Simple %%EOF detection code here! */
      if ( (parse->token == TOKEN_COMMENT) &&
           sbuf_startwith_ws(parse, STRING_AND_LENGTH("%EOF")) ) {
        do_eof(parse);
        state_replace(parse, PDF_INCREMENTAL);
      }
      break;

    case PDF_INCREMENTAL:
      if ( !parse_token(parse) ) {
        return(PDFPARSE_MORE_DATA);
      }
      switch ( parse->token ) {
      case TOKEN_COMMENT:
        /* Look for PDF header or typical PS job strat - %!PS */
        if ( is_pdf_header(parse) ||
             sbuf_startswith(parse, STRING_AND_LENGTH("!PS")) ) {
          /* Looks like a new PS/PDF job - so got our PDF */
          return(PDFPARSE_EOJ);
        }
        /* Bog ordinary comment - look for an object number */
        state_replace(parse, PDF_OBJECT_NUM);
        break;

      case TOKEN_TOKEN:
        if ( sbuf_eq(parse, STRING_AND_LENGTH("xref")) ) {
          /* Found an updated cross-reference table */
          state_replace(parse, PDF_XREF);
          break;

        } else if ( sbuf_eq(parse, STRING_AND_LENGTH("trailer")) ) {
          /* Found a new trailer - consume the trailer dictionary */
          state_replace(parse, PDF_TRAILER_STARTXREF);
          if ( !state_push(parse, PDF_COMPOSITE_DICT) ) {
            return(PDFPARSE_PARSE_ERROR);
          }
          break;
        }
        /* Not a keyword expected at this point, assume another PDL */
        return(PDFPARSE_EOJ);

      case TOKEN_INTEGER:
        /* Most likely an indirect object number - look for generation number */
        state_replace(parse, PDF_OBJECT_GEN);
        break;

      default: /* Nothing we would expect in a valid PDF so punt */
        return(PDFPARSE_EOJ);
      }
      break;
    }
  }

  /* To check that there are no paths to the end of this function enable the
   * following code to be compiled - you should get a warning about unreachable
   * code - there is a problem if you don't */
#if 0
  /* NEVERREACHED */
  return(PDFPARSE_ERR_UNSPECIFIED);
#endif

} /* pdfparse */


/* Let the parser complete anything it needs to when there is no more input. */
void pdfparse_complete(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "pdfparse_complete: NULL parser pointer");

  /* Cope with incomplete parsing of a terminating %%EOF comment */
  if ( PARSE_STATE(parse) == PDF_TRAILER_EOF ) {
    if ( (parse->tok_state == TOK_PARSE_COMMENT) &&
         sbuf_eq(parse, STRING_AND_LENGTH("%EOF")) ) {
      do_eof(parse);
    }
  }

} /* pdfparse_complete */


/* See if the PDF parser has seen a possible EOF */
int32 pdfparse_eof(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "pdfparse_eof: NULL parser pointer");

  /* Note: it is still possible that we are processing an incremental update, so
   * while we have seen an %%EOF it may not be the one from the end of the job!
   * For example, if we haven't seen at least the same amount of data as
   * recorded in a linearised PDF then we haven't seen at least the end of
   * original job, let alone any updates.  To do this will need to recognise the
   * linearised dictionary and the recorded file length.
   */
  return(parse->seen_eof ? PDFPARSE_EOJ : PDFPARSE_MORE_DATA);

} /* pdfparse_eof */


/* Destroy the PDF parser instance */
void pdfparse_end(
  PDF_PARSE*  parse)
{
  HQASSERT((parse != NULL),
           "pdfparse_end: NULL parser pointer");
  HQASSERT((parse->spooler_if.free != NULL),
           "pdfparse_end: NULL parser spooler free memory pointer");

  parse->spooler_if.free(parse);

} /* pdfparse_end */


/* Log stripped */
