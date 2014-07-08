/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlstream.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 */

#include "core.h"
#include "fileio.h"
#include "hqmemcmp.h"
#include "swctype.h"

#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxltags.h"
#include "pclxlstream.h"
#include "pclxluserstream.h"

/* Macros to wrap reading of bytes, 16-bit words, and 32-bit words according to
 * endianness.  They return immediately on EOF.
 */
#define STREAM_READ_BYTE(s, b) \
MACRO_START \
  int32 ch; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (b) = CAST_SIGNED_TO_UINT8(ch); \
MACRO_END

#define STREAM_READ_SBYTE(s, b) \
MACRO_START \
  int32 ch; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (b) = (int8)ch; \
MACRO_END

#define STREAM_READ_WORD_BE(s, w) \
MACRO_START \
  int32 ch; \
  uint32 word; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  word = CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (w) = CAST_SIGNED_TO_UINT16((word << 8) | CAST_SIGNED_TO_UINT8(ch)); \
MACRO_END

#define STREAM_READ_WORD_LE(s, w) \
MACRO_START \
  int32 ch; \
  uint32 word; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  word = CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (w) = CAST_SIGNED_TO_UINT16((CAST_SIGNED_TO_UINT8(ch) << 8) | word); \
MACRO_END

#define STREAM_READ_WORD(s, b, w) \
MACRO_START \
  if ( (b) ) { \
    STREAM_READ_WORD_BE((s), (w)); \
  } else { \
    STREAM_READ_WORD_LE((s), (w)); \
  } \
MACRO_END

#define STREAM_READ_SWORD_BE(s, w) \
MACRO_START \
  int32 ch; \
  int32 word; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  word = (int8)ch; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (w) = (word << 8) | CAST_SIGNED_TO_UINT8(ch); \
MACRO_END

#define STREAM_READ_SWORD_LE(s, w) \
MACRO_START \
  int32 ch; \
  int32 word; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  word = CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (w) = ((int8)ch << 8) | word; \
MACRO_END

#define STREAM_READ_SWORD(s, b, w) \
MACRO_START \
  if ( (b) ) { \
    STREAM_READ_SWORD_BE((s), (w)); \
  } else { \
    STREAM_READ_SWORD_LE((s), (w)); \
  } \
MACRO_END

#define STREAM_READ_DWORD_BE(s, d) \
MACRO_START \
  int32 ch; \
  uint32 dword; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = (dword << 8) | CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = (dword << 8) | CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (d) = (dword << 8) | CAST_SIGNED_TO_UINT8(ch); \
MACRO_END

#define STREAM_READ_DWORD_LE(s, d) \
MACRO_START \
  int32 ch; \
  uint32 dword; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = CAST_SIGNED_TO_UINT8(ch); \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = (CAST_SIGNED_TO_UINT8(ch) << 8) | dword; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  dword = (CAST_SIGNED_TO_UINT8(ch) << 16) | dword; \
  if ( (ch = Getc((s)->flptr)) == EOF ) { \
    return(FALSE); \
  } \
  (d) = (CAST_SIGNED_TO_UINT8(ch) << 24) | dword; \
MACRO_END

#define STREAM_READ_DWORD(s, b, d) \
MACRO_START \
  if ( (b) ) { \
    STREAM_READ_DWORD_BE((s), (d)); \
  } else { \
    STREAM_READ_DWORD_LE((s), (d)); \
  } \
MACRO_END


/* Match a specific byte sequence in the stream */
static
Bool match_bytes(
  PCLXLSTREAM*  p_stream,
  uint8*        str,
  int32         str_len)
{
  int32 ch;

  HQASSERT((p_stream != NULL),
           "match_bytes: NULL stream pointer");
  HQASSERT((str != NULL),
           "match_bytes: NULL string pointer");
  HQASSERT((str_len > 0),
           "match_bytes: invalid string length");

  while ( str_len-- > 0 ) {
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      return(FALSE);
    }
    if ( ch != *str++ ) {
      return(FALSE);
    }
  }

  return(TRUE);

} /* match_bytes */


/* Match a string to a whole header field - terminating field separator is
 * consumed.  Any unexpected byte (including early field terminator or header
 * terminator) will cause the match to fail.
 * TRUE iff string matches field contents followed by field terminator else
 * FALSE.
 * Failing to match any of the characters in the class name results in an
 * unsupported class name error, unless it is the escape character, when it is a
 * illegal stream header, even if it is not followed by the rest of a UEL.
 */
static
Bool header_field_match_classname(
  PCLXLSTREAM*  p_stream,
  uint8*        str,
  int32         str_len,
  int32*        p_error)
{
  int32 ch;

  HQASSERT((p_stream != NULL),
           "header_field_match_classname: NULL stream pointer");
  HQASSERT((str != NULL),
           "header_field_match_classname: NULL string pointer");
  HQASSERT((str_len > 0),
           "header_field_match_classname: invalid string length");
  HQASSERT((p_error != NULL),
           "header_field_match_classname: NULL pointer to returned error");

  /* Match the class name */
  while ( str_len-- > 0 ) {
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      *p_error = PCLXL_UNSUPPORTED_CLASS_NAME;
      return(FALSE);
    }
    if ( ch != *str++ ) {
      if ( ch == ESC ) {
        *p_error = PCLXL_ILLEGAL_STREAM_HEADER;
      } else {
        *p_error = PCLXL_UNSUPPORTED_CLASS_NAME;
      }
      return(FALSE);
    }
  }

  /* Match the field terminator */
  ch = Getc(p_stream->flptr);
  if ( ch != ';' ) {
    *p_error = PCLXL_ILLEGAL_STREAM_HEADER;
    return(FALSE);
  }
  return(TRUE);

} /* header_field_match_classname */


/* Parse an integer valued stream header field.
 * Returns TRUE if parsed a number followed by a field separator or the header
 * terminator.
 * Does not handle overflow.
 */
static
Bool header_field_parse_number(
  PCLXLSTREAM*  p_stream,
  int32*        p_number,
  Bool*         p_seen_lf)
{
  int32   number;
  int32   ch;

  HQASSERT((p_stream != NULL),
           "header_field_parse_number: NULL stream pointer");
  HQASSERT((p_number != NULL),
           "header_field_parse_number: NULL pointer to returned number");
  HQASSERT((p_seen_lf != NULL),
           "header_field_parse_number: NULL pointer to returned LF flag");

  if ( (ch = Getc(p_stream->flptr)) == EOF ) {
    return(FALSE);
  }
  if ( !isdigit(ch) ) {
    return(FALSE);
  }
  number = 0;
  while ( isdigit(ch) ) {
    number = number*10 + ch - '0';
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      return(FALSE);
    }
  }
  *p_number = number;
  /* Check for field separator - ; */
  if (ch == ';') {
    *p_seen_lf = FALSE;
    return(TRUE);
  }

  /* Check for header terminator - [CR] + LF */
  if (ch == CR) {
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      return(FALSE);
    }
  }

  *p_seen_lf = (ch == LF);
  return(*p_seen_lf);

} /* header_field_parse_number */


/* Read stream up to stream header terminator. */
static
Bool header_flush(
  PCLXLSTREAM*  p_stream)
{
  int32 ch;

  HQASSERT((p_stream != NULL),
           "header_flush: NULL stream pointer");

  do {
    if ( ((ch = Getc(p_stream->flptr)) == EOF) ||
         (ch == ESC) ) {
      return(FALSE);
    }
  } while ( ch != LF );

  return(TRUE);

} /* header_flush */


static
Bool parse_stream_header(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  int32         binding)
{
  int32   protocol_class;
  int32   protocol_revision;
  int32   ch;
  int32   error;
  Bool    seen_lf;

  HQASSERT((p_stream != NULL),
           "parse_stream_header: NULL stream pointer");

  /* Only support big and little endian bindings */
  if ( (binding != '(') && (binding != ')') ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_UNSUPPORTED_BINDING,
                        ("Stream encoding 0x%02x not supported", binding));
    return(FALSE);
  }
  p_stream->big_endian = (binding == '(');
  if ( p_stream->big_endian &&
       ((pclxl_context->config_params.stream_endianness_supported&PCLXL_STREAM_ENDIANNESS_BIG) == 0) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_UNSUPPORTED_BINDING,
                        ("Big endian encoding not supported"));
    return(FALSE);
  }

  /* Reserved byte, should be a space */
  if ( (ch = Getc(p_stream->flptr)) != ' ' ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_STREAM_HEADER,
                        ("EOF reading stream header(1)"));
    return(FALSE);
  }

  /* Match the class name */
  if ( !header_field_match_classname(p_stream, NAME_AND_LENGTH("HP-PCL XL"), &error) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, error,
                        ("Failed to parse class name"));
    return(FALSE);
  }

  /* Read protocol class and revision */
  if ( !header_field_parse_number(p_stream, &protocol_class, &seen_lf) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_STREAM_HEADER,
                        ("Failed to read protocol class"));
    return(FALSE);
  }
  if ( seen_lf ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_STREAM_HEADER,
                        ("EndOfHeader seen protocol class"));
    return(FALSE);
  }
  if ( !header_field_parse_number(p_stream, &protocol_revision, &seen_lf) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_STREAM_HEADER,
                        ("Failed to read protocol revision"));
    return(FALSE);
  }
  p_stream->protocol_version = PCLXL_PACK_PROTOCOL_CLASS_AND_REVISION(protocol_class, protocol_revision);
  if ( (protocol_class > 3) || (protocol_revision > 1) ||
       ((p_stream->protocol_version != PCLXL_PROTOCOL_VERSION_1_0) &&
        (p_stream->protocol_version != PCLXL_PROTOCOL_VERSION_1_1) &&
        (p_stream->protocol_version != PCLXL_PROTOCOL_VERSION_2_0) &&
        (p_stream->protocol_version != PCLXL_PROTOCOL_VERSION_2_1) &&
        (p_stream->protocol_version != PCLXL_PROTOCOL_VERSION_3_0)) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_UNSUPPORTED_PROTOCOL_VERSION,
                        ("Invalid protocol class %d revision %d", protocol_class, protocol_revision));
    return(FALSE);
  }

  /* Skip to end of header */
  if ( !seen_lf && !header_flush(p_stream) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_STREAM_HEADER,
                        ("Failed to find EndOfHeader"));
    return(FALSE);
  }

  return(TRUE);

} /* parse_stream_header */


/* Initialise an XL stream with default values. */
void pclxl_stream_init(
  PCLXLSTREAM*  p_stream)
{
  HQASSERT((p_stream != NULL),
           "pclxl_stream_init: NULL stream pointer");

  SLL_RESET_LINK(p_stream, next);
  p_stream->flptr = NULL;
  p_stream->big_endian = FALSE;
  p_stream->last_tag = 0;
  p_stream->op_counter = 0;
  p_stream->protocol_version = 0;

} /* pclxl_stream_init */


/* Start an XL stream on the file list. */
Bool pclxl_stream_start(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  FILELIST*     flptr)
{
  int32 ch;

  HQASSERT((p_stream != NULL),
           "pclxl_stream_start: NULL stream pointer");
  HQASSERT((flptr != NULL),
           "pclxl_stream_start: NULL file pointer");

  p_stream->flptr = flptr;

  if ( (ch = Getc(flptr)) == EOF ) {
    return(FALSE);
  }
  p_stream->last_tag = ch;
  if ( is_xlbinding(ch) ) {
    /* Set stream binding */
    if ( !parse_stream_header(pclxl_context, p_stream, ch) ) {
      return(FALSE);
    }
    return(TRUE);
  }
  if ( ch == ESC ) {
    /* Look for a full UEL and close the stream if seen */
    if ( !match_bytes(p_stream, NAME_AND_LENGTH("%-12345X")) ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                          ("Encountered ESC not starting a UEL"));
      return(FALSE);
    }
    SetIEofFlag(p_stream->flptr);
    theIMyCloseFile(p_stream->flptr)(p_stream->flptr, CLOSE_EXPLICIT);
    return(FALSE);
  }
  /* Anything else is an error */
  PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                      ("Unexpected tag byte %02x", ch));
  return(FALSE);

} /* pclxl_stream_start */


/* Return the next tag for a datatype, attribute, or operator in the stream. */
Bool pclxl_stream_next_tag(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_tag)
{
  int32   ch;

  HQASSERT((p_stream != NULL),
           "pclxl_stream_next_tag: NULL stream pointer");
  HQASSERT((p_tag != NULL),
           "pclxl_stream_next_tag: NULL pointer to returned tag");

  for (;;) {
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      return(FALSE);
    }
    /* Remember tag byte seen for error reporting */
    p_stream->last_tag = ch;
    if ( is_xldatatype(ch) || is_xlattribute(ch) ) {
      /* Return datatype/attribute tag */
      *p_tag = ch;
      return(TRUE);
    }
    if ( is_xloperator(ch) ) {
      /* Count new operator and return it */
      ++p_stream->op_counter;
      *p_tag = ch;
      return(TRUE);
    }
    if ( ch == ESC ) {
      /* Look for a full UEL and close the stream if seen */
      if ( !match_bytes(p_stream, NAME_AND_LENGTH("%-12345X")) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                            ("Encountered ESC not starting a UEL"));
        return(FALSE);
      }
      SetIEofFlag(p_stream->flptr);
      theIMyCloseFile(p_stream->flptr)(p_stream->flptr, CLOSE_EXPLICIT);
      return(FALSE);
    }
    if ( is_xlbinding(ch) ) {
      /* Update stream binding */
      if ( !parse_stream_header(pclxl_context, p_stream, ch) ) {
        return(FALSE);
      }
      continue;
    }
    if ( !is_xlwhitespace(ch) ) {
      /* Anything else is an error */
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                          ("Unexpected tag byte %02x", ch));
      return(FALSE);
    }
  }
  /* NEVERREACHED */

} /* pclxl_stream_next_tag */


/* Read an attribute id from the stream. */
Bool pclxl_stream_read_attribute(
  PCLXLSTREAM*  p_stream,
  uint32        type_tag,
  uint32*       p_attribute)
{
  HQASSERT((p_stream != NULL),
           "pclxl_stream_read_attribute: NULL stream pointer");
  HQASSERT((p_attribute != NULL),
           "pclxl_stream_read_attribute: NULL pointer to returned attribute");

  if ( type_tag == XL_TAG_ATTRIBUTE_BYTE ) {
    STREAM_READ_BYTE(p_stream, *p_attribute);

  } else {
    HQASSERT((type_tag == XL_TAG_ATTRIBUTE_WORD),
             "pclxl_stream_read_attribute: unknown attribute datatype tag");
    STREAM_READ_WORD(p_stream, p_stream->big_endian, *p_attribute);
  }

  return(TRUE);

} /* pclxl_stream_read_attribute */


/* Read values from a stream based on the datatype tag. */
Bool pclxl_stream_read_data(
  PCLXLSTREAM*  p_stream,
  uint32        datatype_tag,
  void*         buffer,
  uint32        count)
{
  uint8*  bdata;
  uint16* wdata;
  uint32* dwdata;

  HQASSERT((p_stream != NULL),
           "pclxl_stream_read_data: NULL stream pointer");
  HQASSERT((buffer != NULL),
           "pclxl_stream_read_data: NULL buffer pointer");
  HQASSERT((count > 0),
           "pclxl_stream_read_data: invalid element count");

  switch ( XL_TAG_TO_TYPE(datatype_tag) ) {
  case XL_TAG_TYPE_UBYTE:
    bdata = (uint8*)buffer;
    while ( count-- > 0 ) {
      STREAM_READ_BYTE(p_stream, *bdata++);
    }
    break;

  case XL_TAG_TYPE_UINT16:
  case XL_TAG_TYPE_SINT16:
    wdata = (uint16*)buffer;
    if ( p_stream->big_endian ) {
      while ( count-- > 0 ) {
        STREAM_READ_WORD_BE(p_stream, *wdata++);
      }
    } else {
      while ( count-- > 0 ) {
        STREAM_READ_WORD_LE(p_stream, *wdata++);
      }
    }
    break;

  case XL_TAG_TYPE_UINT32:
  case XL_TAG_TYPE_SINT32:
  case XL_TAG_TYPE_REAL32:
    dwdata = (uint32*)buffer;
    if ( p_stream->big_endian ) {
      while ( count-- > 0 ) {
        STREAM_READ_DWORD_BE(p_stream, *dwdata++);
      }
    } else {
      while ( count-- > 0 ) {
        STREAM_READ_DWORD_LE(p_stream, *dwdata++);
      }
    }
    break;

  default:
    HQFAIL("Unexpected datatype tag");
  }

  return(TRUE);

} /* pclxl_stream_read_data */


/* Read length of a value array. */
Bool pclxl_stream_read_array_length(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_length)
{
  uint32 type_tag;

   HQASSERT((p_stream != NULL),
            "pclxl_stream_read_array_length: NULL stream pointer");
   HQASSERT((p_length != NULL),
            "pclxl_stream_read_array_length: NULL length pointer");

  if ( !pclxl_stream_next_tag(pclxl_context, p_stream, &type_tag) ) {
    return(FALSE);
  }

  if ( type_tag == XL_TAG_SCALAR_UBYTE ) {
    STREAM_READ_BYTE(p_stream, *p_length);

  } else if ( type_tag == XL_TAG_SCALAR_UINT16 ) {
    STREAM_READ_WORD(p_stream, p_stream->big_endian, *p_length);

  } else {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                        ("Invalid tag when reading array length type - %02x", type_tag));
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_stream_read_array_length */


/* Look for embedded data length data type tag */
static
Bool pclxl_stream_next_embedded_tag(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_tag)
{
  int32   ch;

  HQASSERT((p_stream != NULL),
            "pclxl_stream_next_embedded_tag: NULL stream pointer");
   HQASSERT((p_tag != NULL),
            "pclxl_stream_next_embedded_tag: NULL tag pointer");

  for (;;) {
    if ( (ch = Getc(p_stream->flptr)) == EOF ) {
      return(FALSE);
    }
    if ( is_xlembedded(ch) ) {
      /* Return datatype/attribute tag */
      *p_tag = ch;
      return(TRUE);
    }
    if ( ch == ESC ) {
      /* Look for a full UEL and close the stream if seen */
      if ( !match_bytes(p_stream, NAME_AND_LENGTH("%-12345X")) ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                            ("Encountered ESC not starting a UEL"));
        return(FALSE);
      }
      theIMyCloseFile(p_stream->flptr)(p_stream->flptr, CLOSE_EXPLICIT);
      continue;
    }
    if ( !is_xlwhitespace(ch) ) {
      /* Anything else is an error */
      p_stream->last_tag = ch;
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                          ("Unexpected tag byte %02x", ch));
      return(FALSE);
    }
  }
  /* NEVERREACHED */

} /* pclxl_stream_next_embedded_tag */


/* Read length of embedded data. */
Bool pclxl_stream_read_data_length(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  uint32*       p_length)
{
  uint32 len_type_tag;

  HQASSERT((p_stream != NULL),
            "pclxl_stream_read_data_length: NULL stream pointer");
  HQASSERT((p_length != NULL),
            "pclxl_stream_read_data_length: NULL length pointer");

  if ( !pclxl_stream_next_embedded_tag(pclxl_context, p_stream, &len_type_tag) ) {
    return(FALSE);
  }

  /* AFAIK embedded data read from the data source has its length in the
   * endianess of the stream, not the data source!
   */
  if ( len_type_tag == XL_TAG_EMBEDDED_BYTE ) {
    STREAM_READ_BYTE(p_stream, *p_length);

  } else if ( len_type_tag == XL_TAG_EMBEDDED_UINT32 ) {
    STREAM_READ_DWORD(p_stream, p_stream->big_endian, *p_length);

  } else {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                        ("Invalid tag when reading embedded data length type - %02x", len_type_tag));
    return(FALSE);
  }

  return(TRUE);

} /* pclxl_stream_read_data_length */


/* Initialise an embedded reader on the stream with the given data organisation. */
Bool pclxl_stream_embedded_init(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream,
  Bool          big_endian,
  PCLXL_EMBEDDED_READER* p_reader)
{
  uint32 type_tag;

  HQASSERT((p_stream != NULL),
           "pclxl_stream_embedded_init: NULL stream pointer");
  HQASSERT((p_reader != NULL),
           "pclxl_stream_embedded_init: NULL embedded reader pointer");

  /** \todo Wrong place - all callers should be checking!! */
  if ( p_stream == NULL ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_DATA_SOURCE_NOT_OPEN,
                        ("Data source is not open"));
    return(FALSE);
  }

  if ( !pclxl_stream_next_embedded_tag(pclxl_context, p_stream, &type_tag) ) {
    return(FALSE);
  }

  /* AFAIK embedded data read from the data source has its length in the
   * endianess of the stream, not the data source!
   */
  if ( type_tag == XL_TAG_EMBEDDED_BYTE ) {
    STREAM_READ_BYTE(p_stream, p_reader->length);

  } else if ( type_tag == XL_TAG_EMBEDDED_UINT32 ) {
    STREAM_READ_DWORD(p_stream, p_stream->big_endian, p_reader->length);

  } else {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_TAG,
                        ("Invalid tag when reading embedded data length type - %02x", type_tag));
    return(FALSE);
  }

  p_reader->p_stream = p_stream;
  p_reader->big_endian = big_endian;
  p_reader->remaining = p_reader->length;
  p_reader->insufficient = FALSE;

  return(TRUE);

} /* pclxl_stream_embedded_init */

/* ============================================================================
 * PCL XL operators below here.
 * ============================================================================
 */

/* Flush the embedded data of any remaining data. */
Bool pclxl_embedded_flush(
  PCLXL_EMBEDDED_READER*  p_embedded)
{
  int32 dummy;

  HQASSERT((p_embedded != NULL),
           "pclxl_embedded_flush: NULL embedded reader pointer");
  HQASSERT((!p_embedded->insufficient),
           "pclxl_embedded_flush: flushing after a failed read");

  while ( p_embedded->remaining-- > 0 ) {
    STREAM_READ_BYTE(p_embedded->p_stream, dummy);
  }

  return(TRUE);

} /* pclxl_embedded_flush */


/* Read number of data values from the embedded data. */
Bool pclxl_embedded_read_data(
  PCLXL_EMBEDDED_READER*  p_embedded,
  uint32                  datatype,
  int32*                  buffer,
  uint32                  count)
{
  HQASSERT((p_embedded != NULL),
           "pclxl_embedded_read_data: NULL embedded data pointer");
  HQASSERT((datatype >= XL_DATATYPE_UBYTE && datatype <= XL_DATATYPE_SINT16),
           "pclxl_embedded_read_data: datatype out of range");
  HQASSERT((buffer != NULL),
           "pclxl_embedded_read_data: NULL buffer pointer");
  HQASSERT((count > 0),
           "pclxl_embedded_read_data: invalid count");
  HQASSERT((!p_embedded->insufficient),
           "pclxl_embedded_read_data: already failed a read");

  switch ( datatype ) {
  case XL_DATATYPE_UBYTE:
    if ( count > p_embedded->remaining ) {
      p_embedded->insufficient = TRUE;
      return(FALSE);
    }
    p_embedded->remaining -= count;
    while ( count-- > 0 ) {
      STREAM_READ_BYTE(p_embedded->p_stream, *buffer++);
    }
    break;

  case XL_DATATYPE_SBYTE:
    if ( count > p_embedded->remaining ) {
      p_embedded->insufficient = TRUE;
      return(FALSE);
    }
    p_embedded->remaining -= count;
    while ( count-- > 0 ) {
      STREAM_READ_SBYTE(p_embedded->p_stream, *buffer++);
    }
    break;

  case XL_DATATYPE_UINT16:
    if ( 2*count > p_embedded->remaining ) {
      p_embedded->insufficient = TRUE;
      return(FALSE);
    }
    p_embedded->remaining -= 2*count;
    if ( p_embedded->big_endian ) {
      while  ( count-- > 0 ) {
        STREAM_READ_WORD_BE(p_embedded->p_stream, *buffer++);
      }
    } else {
      while  ( count-- > 0 ) {
        STREAM_READ_WORD_LE(p_embedded->p_stream, *buffer++);
      }
    }
    break;

  case XL_DATATYPE_SINT16:
    if ( 2*count > p_embedded->remaining ) {
      p_embedded->insufficient = TRUE;
      return(FALSE);
    }
    p_embedded->remaining -= 2*count;
    if ( p_embedded->big_endian ) {
      while  ( count-- > 0 ) {
        STREAM_READ_SWORD_BE(p_embedded->p_stream, *buffer++);
      }
    } else {
      while  ( count-- > 0 ) {
        STREAM_READ_SWORD_LE(p_embedded->p_stream, *buffer++);
      }
    }
    break;
  }

  return(TRUE);

} /* pclxl_embedded_read_data */


/* Read number of bytes from the embedded data. */
Bool pclxl_embedded_read_bytes(
  PCLXL_EMBEDDED_READER*  p_embedded,
  uint8*                  buffer,
  uint32                  len)
{
  HQASSERT((p_embedded != NULL),
           "pclxl_embedded_read_bytes: NULL embedded reader pointer");
  HQASSERT((buffer != NULL),
           "pclxl_embedded_read_bytes: NULL buffer pointer");
  HQASSERT((len > 0),
           "pclxl_embedded_read_bytes: invalid byte count");
  HQASSERT((!p_embedded->insufficient),
           "pclxl_embedded_read_bytes: already failed a read");
  HQASSERT((p_embedded->p_stream != NULL),
           "pclxl_embedded_read_bytes: NULL embedded stream pointer");

  if ( p_embedded->remaining < len ) {
    p_embedded->insufficient = TRUE;
    return(FALSE);
  }
  p_embedded->remaining -= len;
  return(file_read(p_embedded->p_stream->flptr, buffer, len, NULL) > 0);

} /* pclxl_embedded_read_bytes */

/* This is dirty hack to allow the user stream cache to work - this should be
 * being done as part of its FILELIST close function!
 */
void pclxl_stream_close(
  PCLXL_CONTEXT pclxl_context,
  PCLXLSTREAM*  p_stream)
{
  HQASSERT((p_stream != NULL),
            "pclxl_stream_close: NULL stream pointer");

  close_stream(pclxl_context, &p_stream->flptr);
}

/* Log stripped */
