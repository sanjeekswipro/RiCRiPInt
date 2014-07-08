/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdftextstr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements support functions for PDF text string data structures.
 * (Text streams as well when we need them.)
 */

#include "core.h"
#include "swerrors.h"     /* RANGECHECK */

#include "pdfin.h"        /* pdf_ixc_params */
#include "pdftextstr.h"


/** \brief PDFDoc encoding. */
#define PDF_TEXT_ENCODING_PDFDOC  (1)
/** \brief Unicode UTF-16BE encoding. */
#define PDF_TEXT_ENCODING_UTF16BE (2)
/** \brief Unicode UTF-16LE encoding. */
#define PDF_TEXT_ENCODING_UTF16LE (3)
/** \brief Unicode UTF-8 encoding. */
#define PDF_TEXT_ENCODING_UTF8    (4)

/**
 * \brief
 * The name of a recognised encoding of a PDF text string, and the length of the
 * name.
 */
typedef struct PDF_TEXT_ENCODING {
  uint8*  name;       /**< Name of encoding. */
  int32   len;        /**< Length of encoding name. */
} PDF_TEXT_ENCODING;

#define ENCODING_NAME(s)   {(uint8*)("" s ""), sizeof("" s "") - 1}

/**
 * \brief
 * An array of encoding names and their lengths that can be indexed with the
 * values of the various \c PDF_TEXT_ENCODING_* constants.
 */
static PDF_TEXT_ENCODING ptx_encoding[] = {
  ENCODING_NAME(""),          /* Never used */
  ENCODING_NAME("PDFDoc"),
  ENCODING_NAME("UTF-16BE"),
  ENCODING_NAME("UTF-16LE"),
  ENCODING_NAME("UTF-8")
};


/**
 * \brief Detect any encoding BOMs in the text string.
 *
 * \param[in] string
 * Pointer to PS object containing text string byte sequence.
 *
 * \returns
 * One of the \c PDF_TEXT_ENCODING_* constants to indicate the encoding detected.
 */
static
int32 pdf_text_encoding(
/*@in@*/ /*@notnull@*/
  OBJECT* string)
{
  int32 bom;

  HQASSERT((string != NULL),
           "pdf_text_encoding: NULL PDF string pointer");
  HQASSERT((oType(*string) == OSTRING),
           "pdf_text_encoding: not string when expected");

  if ( theILen(string) > 1 ) {
    bom = (((uint8)oString(*string)[0]) << 8) | ((uint8)oString(*string)[1]);
    switch ( bom ) {
    case 0xfeff:
      return(PDF_TEXT_ENCODING_UTF16BE);

    case 0xfffe:
      return(PDF_TEXT_ENCODING_UTF16LE);

    case 0xefbb:
      if ( (theILen(string) > 2) && (oString(*string)[2] == 0xbf) ) {
        return(PDF_TEXT_ENCODING_UTF8);
      }
    }
  }

  return(PDF_TEXT_ENCODING_PDFDOC);

} /* pdf_text_encoding */


/**
 * \brief Scan a UTF-16BE string for an escape character.
 *
 * \param[in] p_input
 * A pointer to the start of the unicode string.
 * \param[in] len
 * The length of the unicode string in bytes.
 *
 * \returns
 * The offset in bytes to an escape character in the string, or the end of the
 * string.
 */
static
int32 ptx_scan_escape_utf16be_start(
/*@in@*/ /*@notnull@*/
  uint8*  p_input,
  int32   len)
{
  int32   utf_16be;
  int32   f_seen_esc;
  uint8*  p_utf16_be;

  HQASSERT((p_input != NULL),
           "ptx_scan_escape_utf16be_start: NULL input pointer");
  HQASSERT((len > 0),
           "ptx_scan_escape_utf16be_start: invalid input length");

  p_utf16_be = p_input;

  f_seen_esc = FALSE;
  while ( len > 1 ) {
    utf_16be = (p_utf16_be[0] << 8) | p_utf16_be[1];
    if ( utf_16be == 0x001bu ) {
      f_seen_esc = TRUE;
      break;
    }
    p_utf16_be += 2;
    len -= 2;
  }

  if ( (len == 1) && !f_seen_esc ) {
    /* Haven't seen escape and not enough left to be one, eat last character */
    p_utf16_be++;
  }

  return((int32)(p_utf16_be - p_input));

} /* ptx_scan_escape_utf16be_start */


/**
 * \brief Scan a UTF-16BE string for the end of an embedded escape sequence.
 *
 * The original string must start with the start escape character of the escape
 * sequence.
 *
 * \param[in] pp_input
 * Pointer to pointer to string - updated to point to immediately after end of
 * escape sequence.
 * \param[in] p_len
 * Pointer to length of string to scan - updated with length of remaining
 * string.
 */
static
void ptx_scan_escape_utf16be_end(
/*@in@*/ /*@notnull@*/
  uint8** pp_input,
/*@in@*/ /*@notnull@*/
  int32*  p_len)
{
  int32   len;
  int32   f_seen_esc;
  uint8*  p_utf16_be;

  HQASSERT((pp_input != NULL),
           "ptx_scan_escape_utf16be_end: NULL pointer to input pointer");
  HQASSERT((*pp_input != NULL),
           "ptx_scan_escape_utf16be_end: NULL input pointer");
  HQASSERT((p_len != NULL),
           "ptx_scan_escape_utf16be_end: NULL pointer to input length");
  HQASSERT((*p_len > 2),
           "ptx_scan_escape_utf16be_end: len smaller than initial ESC!");

  p_utf16_be = *pp_input;
  len = *p_len;

  /* Skip leading escape */
  p_utf16_be += 2 ;
  len -= 2;

  /* Look for terminating ESC - no assumptions on validity of escape sequence
   * content or length.
   */
  f_seen_esc = FALSE;
  while ( len-- > 1 ) {
    if ( *p_utf16_be++ == 0 ) {
      len -= 1;
      if ( *p_utf16_be++ == 0x1b ) {
        f_seen_esc = TRUE;
        break;
      }
    }
  }

  if ( (len == 1) && !f_seen_esc ) {
    /* Haven't seen escape and not enough left to be one, eat last character */
    p_utf16_be++;
    len--;
  }

  /* Return pointer and len after terminating escape, or end of input */
  *pp_input = p_utf16_be;
  *p_len = len;

} /* ptx_scan_escape_utf16be_end */


/**
 * \brief Scan a UTF-16LE string for an escape character.
 *
 * \param[in] p_input
 * A pointer to the start of the unicode string.
 * \param[in] len
 * The length of the unicode string in bytes.
 *
 * \returns
 * The offset in bytes to an escape character in the string, or the end of the
 * string.
 */
static
int32 ptx_scan_escape_utf16le_start(
/*@in@*/ /*@notnull@*/
  uint8*  p_input,
  int32   len)
{
  int32   utf_16le;
  int32   f_seen_esc;
  uint8*  p_utf16_le;

  HQASSERT((p_input != NULL),
           "ptx_scan_escape_utf16le_start: NULL input pointer");
  HQASSERT((len > 0),
           "ptx_scan_escape_utf16le_start: invalid input length");

  p_utf16_le = p_input;

  f_seen_esc = FALSE;
  while ( len > 1 ) {
    utf_16le = (p_utf16_le[1] << 8) | p_utf16_le[0];
    if ( utf_16le == 0x001bu ) {
      f_seen_esc = TRUE;
      break;
    }
    p_utf16_le += 2;
    len -= 2;
  }

  if ( (len == 1) && !f_seen_esc ) {
    /* Haven't seen escape and not enough left to be one, eat last character */
    p_utf16_le++;
  }

  return((int32)(p_utf16_le - p_input));

} /* ptx_scan_escape_utf16le_start */


/**
 * \brief Scan a UTF-16LE string for the end of an embedded escape sequence.
 *
 * The original string must start with the start escape character of the escape
 * sequence.
 *
 * \param[in] pp_input
 * Pointer to pointer to string - updated to point to immediately after end of
 * escape sequence.
 * \param[in] p_len
 * Pointer to length of string to scan - updated with length of remaining
 * string.
 */
static
void ptx_scan_escape_utf16le_end(
/*@in@*/ /*@notnull@*/
  uint8** pp_input,
/*@in@*/ /*@notnull@*/
  int32*  p_len)
{
  int32   len;
  int32   f_seen_esc;
  uint8*  p_utf16_le;

  HQASSERT((pp_input != NULL),
           "ptx_scan_escape_utf16le_end: NULL pointer to input pointer");
  HQASSERT((*pp_input != NULL),
           "ptx_scan_escape_utf16le_end: NULL input pointer");
  HQASSERT((p_len != NULL),
           "ptx_scan_escape_utf16le_end: NULL pointer to input length");
  HQASSERT((*p_len > 2),
           "ptx_scan_escape_utf16le_end: len smaller than initial ESC!");

  p_utf16_le = *pp_input;
  len = *p_len;

  /* Skip leading UTF16 escape */
  p_utf16_le += 2 ;
  len -= 2;

  /* Look for terminating ESC - no assumptions on validity of escape sequence
   * content or length.
   */
  f_seen_esc = FALSE;
  while ( len-- > 1 ) {
    if ( *p_utf16_le++ == 0x1b ) {
      len -= 1;
      if ( *p_utf16_le++ == 0 ) {
        f_seen_esc = TRUE;
        break;
      }
    }
  }

  if ( (len == 1) && !f_seen_esc ) {
    /* Haven't seen escape and not enough left to be one, eat last character */
    p_utf16_le++;
    len--;
  }

  /* Return pointer and len after terminating escape, or end of input */
  *pp_input = p_utf16_le;
  *p_len = len;

} /* ptx_scan_escape_utf16le_end */


/**
 * \brief Scan a UTF-8 string for an escape character.
 *
 * \param[in] p_input
 * A pointer to the start of the unicode string.
 * \param[in] len
 * The length of the unicode string in bytes.
 *
 * \returns
 * The offset in bytes to an escape character in the string, or the end of the
 * string.
 */
static
int32 ptx_scan_escape_utf8_start(
/*@in@*/ /*@notnull@*/
  uint8*  p_input,
  int32   len)
{
  uint8*  p_utf8;

  HQASSERT((p_input != NULL),
           "ptx_scan_escape_utf8_start: NULL input pointer");
  HQASSERT((len > 0),
           "ptx_scan_escape_utf8_start: invalid input length");

  p_utf8 = p_input;

  while ( len > 0 ) {
    if ( *p_utf8 == 0x1bu ) {
      break;
    }
    p_utf8 += 1;
    len -= 1;
  }

  return((int32)(p_utf8 - p_input));

} /* ptx_scan_escape_utf8_start */


/**
 * \brief Scan a UTF-8 string for the end of an embedded escape sequence.
 *
 * The original string must start with the start escape character of the escape
 * sequence.
 *
 * \param[in] pp_input
 * Pointer to pointer to string - updated to point to immediately after end of
 * escape sequence.
 * \param[in] p_len
 * Pointer to length of string to scan - updated with length of remaining
 * string.
 */
static
void ptx_scan_escape_utf8_end(
/*@in@*/ /*@notnull@*/
  uint8** pp_input,
/*@in@*/ /*@notnull@*/
  int32*  p_len)
{
  int32   len;
  uint8*  p_utf8;

  HQASSERT((pp_input != NULL),
           "ptx_scan_escape_utf8_end: NULL pointer to input pointer");
  HQASSERT((*pp_input != NULL),
           "ptx_scan_escape_utf8_end: NULL input pointer");
  HQASSERT((p_len != NULL),
           "ptx_scan_escape_utf8_end: NULL pointer to input length");
  HQASSERT((*p_len > 0),
           "ptx_scan_escape_utf8_end: len smaller than initial ESC!");

  p_utf8 = *pp_input;
  len = *p_len;

  /* Skip leading UTF8 escape */
  p_utf8 += 1;
  len -= 1;

  /* Look for terminating ESC - no assumptions on validity of escape sequence
   * content or length.
   */
  while ( (len-- > 0) && (*p_utf8++ == 0x1bu) ) {
    EMPTY_STATEMENT();
  }

  /* Return pointer and len after terminating escape */
  *pp_input = p_utf8;
  *p_len = len;

} /* ptx_scan_escape_utf8_end */


int32 pdf_text_to_utf8(
/*@in@*/ /*@notnull@*/
  PDFCONTEXT*   pdfc,
/*@in@*/ /*@notnull@*/
  OBJECT*       string,
  uint32        utf8_len,
/*@out@*/ /*@notnull@*/
  utf8_buffer*  utf8_string)
{
  int32   input_len;
  int32   output_len;
  int32   encoding;
  int32   seq_len;
  int32   ret;
  uint8*  p_input;
  uint8*  p_output;
  int32   (*ptx_scan_escape_start)(uint8* p, int32 l) = NULL;
  void    (*ptx_scan_escape_end)(uint8** p, int32* l) = NULL;
  unicode_convert_t* p_converter;
  GET_PDFXC_AND_IXC

  HQASSERT((pdfc != NULL),
           "pdf_text_to_utf8: NULL PDF context pointer");
  HQASSERT((string != NULL),
           "pdf_text_to_utf8: NULL pdf text string pointer");
  HQASSERT((utf8_string != NULL),
           "pdf_text_to_utf8: NULL UTF8 buffer pointer");
  HQASSERT((utf8_string->codeunits != NULL),
           "pdf_text_to_utf8: NULL pointer to UTF8 buffer");

  p_input = oString(*string);
  input_len = theLen(*string);

  /* Find the encoding used in the text string */
  encoding = pdf_text_encoding(string);
  if ( ixc->strictpdf &&
       ((encoding == PDF_TEXT_ENCODING_UTF16LE) ||
        (encoding == PDF_TEXT_ENCODING_UTF8)) ) {
    /* Strictly, if not UTF-16BE then PDFDoc */
    encoding = PDF_TEXT_ENCODING_PDFDOC;
  }

  /* Skip any BOM and set up escape sequence scanners */
  switch ( encoding ) {
  case PDF_TEXT_ENCODING_UTF16BE:
    p_input += 2;
    input_len -= 2;
    ptx_scan_escape_start = ptx_scan_escape_utf16be_start;
    ptx_scan_escape_end = ptx_scan_escape_utf16be_end;
    break;
  case PDF_TEXT_ENCODING_UTF16LE:
    p_input += 2;
    input_len -= 2;
    ptx_scan_escape_start = ptx_scan_escape_utf16le_start;
    ptx_scan_escape_end = ptx_scan_escape_utf16le_end;
    break;
  case PDF_TEXT_ENCODING_UTF8:
    p_input += 3;
    input_len -= 3;
    ptx_scan_escape_start = ptx_scan_escape_utf8_start;
    ptx_scan_escape_end = ptx_scan_escape_utf8_end;
    break;
  }

  if ( input_len == 0 ) {
    /* Handle degenerate case of an empty string - seen text strings with just a BOM! */
    utf8_string->unitlength = 0;
    return(TRUE);
  }

  p_output = utf8_string->codeunits;
  output_len = utf8_len;

  do {
    if ( encoding != PDF_TEXT_ENCODING_PDFDOC ) {
      /* Scan for start of embedded escape sequence. */
      seq_len = (ptx_scan_escape_start)(p_input, input_len);

    } else { /* For PDFDoc we convert in one pass */
      seq_len = input_len;
    }

    if ( seq_len > 0 ) {
      /* Convert sequence up to next escape */
      p_converter = unicode_convert_open(ptx_encoding[encoding].name, ptx_encoding[encoding].len,
                                         ptx_encoding[PDF_TEXT_ENCODING_UTF8].name, ptx_encoding[PDF_TEXT_ENCODING_UTF8].len,
                                         UCONVERT_BOM_REMOVE, (uint8*)"?", 1);
      if ( p_converter == NULL ) {
        return(error_handler(UNDEFINED));
      }
      input_len -= seq_len;
      HQASSERT((input_len >= 0),
               "pdf_text_to_utf8: converting more than in original string");
      ret = unicode_convert_buffer(p_converter, &p_input, &seq_len, &p_output, &output_len, TRUE);
      unicode_convert_close(&p_converter);
      switch ( ret ) {
      case UTF_CONVERT_OK:
        break;

      case UTF_CONVERT_OUTPUT_EXHAUSTED:
        /* Don't bother converting any more */
        utf8_string->unitlength = utf8_len;
        return(TRUE);

      case UTF_CONVERT_NOT_CANONICAL:
        HQFAIL("utf-8 encoding error from non utf-8 string");
        /*@fallthrough@*/
      default: /* Unexpected error converting */
        return(error_handler(RANGECHECK));
      }
    }

    if ( (input_len > 0) && (encoding != PDF_TEXT_ENCODING_PDFDOC) ) {
      /* Didn't consume all input, must be due to escape sequence so skip it */
      (ptx_scan_escape_end)(&p_input, &input_len);
    }

  } while ( (encoding != PDF_TEXT_ENCODING_PDFDOC) && (input_len > 0) );

  utf8_string->unitlength = utf8_len - output_len;
  return(TRUE);

} /* pdf_text_to_utf8 */


/* Log stripped */
