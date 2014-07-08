/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlfont.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font/Character operator handling functions
 */

#include <string.h>

#include "core.h"
#include "timing.h"
#include "swctype.h"
#include "swcopyf.h"
#include "swpfinpcl.h"
#include "hqmemcpy.h"
#include "hqmemset.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"
#include "pclxlpsinterface.h"
#include "pclxlfont.h"
#include "pclxlscan.h"

#ifdef DEBUG_BUILD

static uint8*
pclxl_debug_font_name(uint8* font_name,
                      uint32 font_name_len,
                      PCLXL_TAG font_name_type)
{
  if ( !font_name )
  {
    return (uint8*) "<NULL>";
  }
  else if ( font_name_type == PCLXL_DT_UInt16_Array )
  {
    static uint8 two_byte_font_name[51];

    (void) swncopyf(two_byte_font_name,
                    sizeof(two_byte_font_name),
                    (uint8*) "<%d x uint16 characters>",
                    font_name_len);

    return two_byte_font_name;
  }
  else if ( font_name_len < 21 )
  {
    return font_name;
  }
  else
  {
    static uint8 abbrev_font_name[51];

    (void) swncopyf(abbrev_font_name,
                    sizeof(abbrev_font_name),
                    (uint8*) "%c%c%c%c%c ...<%d characters in total>... %c%c%c%c%c",
                    font_name[0],
                    font_name[1],
                    font_name[2],
                    font_name[3],
                    font_name[4],
                    font_name_len,
                    font_name[(font_name_len - 5)],
                    font_name[(font_name_len - 4)],
                    font_name[(font_name_len - 3)],
                    font_name[(font_name_len - 2)],
                    font_name[(font_name_len - 1)]);

    return abbrev_font_name;
  }

  /*NOTREACHED*/
}

#endif

#define PCLXL_INITIAL_FONT_HEADER_BUF_LEN 272

void
pclxl_delete_font_header(PCLXL_FONT_HEADER font_header)
{
  font_header->font_data_stream = NULL;

  if ( (font_header->font_header_data != NULL) &&
       (font_header->font_header_data_alloc_len > PCLXL_INITIAL_FONT_HEADER_BUF_LEN) )
  {
    mm_free(font_header->pclxl_context->memory_pool,
            font_header->font_header_data,
            font_header->font_header_data_alloc_len);

    font_header->font_header_data = ((uint8*) font_header) + (sizeof(PCLXL_FONT_HEADER_STRUCT) + font_header->font_name_len + 1);

    font_header->font_header_data_alloc_len = PCLXL_INITIAL_FONT_HEADER_BUF_LEN;

    font_header->font_header_data_len = 0;
  }

  mm_free(font_header->pclxl_context->memory_pool,
          font_header,
          (sizeof(PCLXL_FONT_HEADER_STRUCT) +
           font_header->font_name_len + 1 +
           PCLXL_INITIAL_FONT_HEADER_BUF_LEN));
}

static PCLXL_FONT_HEADER
pclxl_new_font_header(PCLXL_CONTEXT        pclxl_context,
                      uint8*               font_name,
                      uint32               font_name_len,
                      PCLXL_TAG            font_name_type,
                      int32                font_format,
                      PCLXLSTREAM*         font_data_stream)
{
  PCLXL_FONT_HEADER new_font_header;

  /*
   * For efficiency we are going to allocate enough space
   * for the PCLXL_FONT_HEADER_STRUCT and the font name
   * and the minimum initial font header buffer as a single allocation
   */

  size_t bytes_to_allocate = (sizeof(PCLXL_FONT_HEADER_STRUCT) +
                              font_name_len + 1 +
                              PCLXL_INITIAL_FONT_HEADER_BUF_LEN);


  if ( (new_font_header = mm_alloc(pclxl_context->memory_pool,
                                   bytes_to_allocate,
                                   MM_ALLOC_CLASS_PCLXL_FONT_HEADER)) == NULL )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new PCLXL font [header] structure"));

    return NULL;
  }
  else
  {
    HqMemZero(new_font_header, bytes_to_allocate);

    new_font_header->pclxl_context = pclxl_context;
    new_font_header->font_format = CAST_SIGNED_TO_UINT8(font_format);
    new_font_header->font_name = ((uint8*) new_font_header) + (sizeof(PCLXL_FONT_HEADER_STRUCT));
    new_font_header->font_name_len = font_name_len;
    new_font_header->font_name_type = font_name_type;
    new_font_header->font_header_data = ((uint8*) new_font_header) + (sizeof(PCLXL_FONT_HEADER_STRUCT) + font_name_len + 1);
    new_font_header->font_header_data_alloc_len = PCLXL_INITIAL_FONT_HEADER_BUF_LEN;
    new_font_header->font_header_data_len = 0;

#ifdef DEBUG_BUILD

    HqMemSet8(new_font_header->font_header_data, 0xff, PCLXL_INITIAL_FONT_HEADER_BUF_LEN);

#endif /* DEBUG_BUILD */
  }

  if ( (font_name != NULL) &&
       (font_name_len > 0) )
  {
    /*
     * Note that we *must* use memcpy() to capture the font "name"
     * because in some cases this font "name" is actually binary data
     * So strncpy() might prematurely terminate the copy when it sees a nul byte
     */

    (void) memcpy(new_font_header->font_name, font_name, font_name_len);
  }

  /*
   * But we will still ensure that the extra 1 byte
   * that we allocated to hold this font "name"
   * is filled in with a '\0' (nul) byte
   * So that we can use this as a C string
   */

  new_font_header->font_name[font_name_len] = '\0';

  new_font_header->font_data_stream = font_data_stream;

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("New Font Header for font name \"%s\", font data stream = 0x%08x, font data length = %d, allocated font data buffer length = %d",
               pclxl_debug_font_name(font_name, font_name_len, font_name_type),
               new_font_header->font_data_stream,
               new_font_header->font_header_data_len,
               new_font_header->font_header_data_alloc_len));

  return new_font_header;
}

static Bool
pclxl_prepend_dummy_font_header(PCLXL_CONTEXT     pclxl_context,
                                PCLXL_FONT_HEADER font_header)
{
  static uint8 pclxl_font_header[72] =
  {
    0,0x48,0x10,0x02,0,0,0,0,0,0,0,0,0,0x01,0x02,0x75,
    0,0,0,0,0,0,0,0,0,0xFE,0,0,0,0,0,0,
    0,0,0,0,0,0x20,0,0xFF,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0x10,0,0,0,0,0,0x01,0
  };

  HQASSERT((font_header->font_header_data_len == 0),
           "Cannot prepend PCLXL \"dummy\" font header when there is some existing (\"real\") font header data");

  if ( sizeof(pclxl_font_header) > font_header->font_header_data_alloc_len )
  {
    /*
     * There isn't enough space to hold the dummy font header data
     * So we must allocate some more space,
     * then copy the dummy font header into this newly allocated space
     * and then free the originally allocated space if necessary.
     */

    uint8* new_header_data;
    uint32 new_alloc_len = font_header->font_header_data_alloc_len;

    while ( sizeof(pclxl_font_header) > new_alloc_len ) new_alloc_len *= 2;

    if ( (new_header_data = mm_alloc(pclxl_context->memory_pool,
                                     new_alloc_len,
                                     MM_ALLOC_CLASS_PCLXL_FONT_HEADER)) == NULL )
    {
      /*
       * Oops, the allocation failed
       * We must log a memory allocation failure
       * And return FALSE
       */
      /** \todo
       * The question is:
       * What do we do, if anything, with the existing font header data?
       * Do we free it and possibly the whole font header structure here?
       * Or do we leave it for someone else to do later?
       */

      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_TEXT,
                                 PCLXL_INSUFFICIENT_MEMORY,
                                 ("Failed to allocate space to hold \"dummy\" PCLXL font header (%d bytes) for font name \"%s\"",
                                  sizeof(pclxl_font_header),
                                  font_header->font_name));

      /** \todo If we decide to do the deletion here:
       * pclxl_delete_font_header(font_header);
       * pclxl_context->font_header = NULL;
       */

      return FALSE;
    }

#ifdef DEBUG_BUILD

    HqMemSet8(new_header_data, 0xff, new_alloc_len);

#endif /* DEBUG_BUILD */

    if ( (font_header->font_header_data != NULL) &&
         (font_header->font_header_data_alloc_len > PCLXL_INITIAL_FONT_HEADER_BUF_LEN) )
    {
      mm_free(font_header->pclxl_context->memory_pool,
              font_header->font_header_data,
              font_header->font_header_data_alloc_len);
    }

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Extended font header data buffer for font name \"%s\", font data stream = 0x%08x, font data length = %d, extended font header data buffer length from %d bytes to %d bytes",
                 pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type),
                 font_header->font_data_stream,
                 font_header->font_header_data_len,
                 font_header->font_header_data_alloc_len,
                 new_alloc_len));

    font_header->font_header_data = new_header_data;

    font_header->font_header_data_alloc_len = new_alloc_len;
  }

  (void) memcpy(font_header->font_header_data,
                pclxl_font_header,
                sizeof(pclxl_font_header));

  font_header->font_header_data_len = sizeof(pclxl_font_header);

  return TRUE;
}

static Bool
pclxl_begin_font_header(PCLXL_PARSER_CONTEXT parser_context,
                        uint8*               font_name,
                        uint32               font_name_len,
                        PCLXL_TAG            font_name_data_type,
                        int32                font_format)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( pclxl_context->font_header != NULL )
  {
    /*
     * There is already an open font header
     * This really should not be possible
     * because the BeginFontHeader operator is not valid
     * within the scope of an existing BeginFontHeader/EndFontHeader pair
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_ILLEGAL_OPERATOR_SEQUENCE,
                               ("There is already an open font header for font name \"%s\"",
                                pclxl_context->font_header->font_name));

    return FALSE;
  }

  /** \todo
   *
   * I am not sure whether we pass the font header detail piecemeal
   * across to the UFTS PFIN module
   *
   * Or whether we must collect all the font header data together
   * and then pass it all in one go across to the UFST PFIN module
   *
   * Which we must do determines whether we must begin calling the module here
   * or whether we call it only when we see the EndFontHeader operator
   *
   * For the moment I believe that we will call it later
   *
   * So all we do here is to create a new PCLXL_FONT_HEADER
   */

  if ( (pclxl_context->font_header = pclxl_new_font_header(pclxl_context,
                                                           (uint8*) font_name,
                                                           font_name_len,
                                                           font_name_data_type,
                                                           font_format,
                                                           pclxl_parser_current_stream(parser_context))) == NULL )
  {
    /*
     * We have failed to create a new PCLXL_FONT_HEADER
     * A suitable error has been logged so we simply return FALSE here
     */

    return FALSE;
  }
  else
  {
    probe_begin(SW_TRACE_INTERPRET_PCLXL_FONT, pclxl_context);
    return pclxl_prepend_dummy_font_header(pclxl_context,
                                           pclxl_context->font_header);
  }
}

/*
 * Tag 0x4f BeginFontHeader
 */

Bool
pclxl_op_begin_font_header(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[3] = {
#define BEGINFONTHEADER_FONT_NAME    (0)
    {PCLXL_AT_FontName | PCLXL_ATTR_REQUIRED},
#define BEGINFONTHEADER_FONT_FORMAT  (1)
    {PCLXL_AT_FontFormat | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint8* font_name;
  uint32 font_name_len;
  PCLXL_TAG font_name_dt;
  int32 font_format;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* FontName - uint16 font names allowed if not in strict mode */
  if ( (match[BEGINFONTHEADER_FONT_NAME].result->data_type == PCLXL_DT_UInt16_Array) &&
       pclxl_context->config_params.strict_pclxl_protocol_class ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE,
                        ("Illegal attribute data type - caught uint16 fontname when in strict mode."));
    return(FALSE);
  }
  font_name_dt = match[BEGINFONTHEADER_FONT_NAME].result->data_type;
  pclxl_attr_get_byte_len(match[BEGINFONTHEADER_FONT_NAME].result, &font_name, &font_name_len);
  /* FontFormat */
  font_format = pclxl_attr_get_int(match[BEGINFONTHEADER_FONT_FORMAT].result);

  /* QL CET E102.bin pg 94 checks for an error with a font format of 3.  The XL
   * doc only talks about 0 (bitmap) and 1 (TrueType) so limit to that for now.
   */
  if ( (font_format != 0) && (font_format != 1) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("Invalid FontFormat value"));
    return(FALSE);
  }

  if ( !parser_context->data_source_open ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_DATA_SOURCE_NOT_OPEN,
                        ("Data source not open when reading font header."));
    return(FALSE);
  }

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("BeginFontHeader(FontName=\"%s\", FontFormat = %d)",
               pclxl_debug_font_name(font_name, font_name_len, font_name_dt),
               font_format));

  return(pclxl_begin_font_header(parser_context, font_name, font_name_len,
                                 font_name_dt, font_format));
}

/**
 * \brief pclxl_read_font_header() actually only reads a *segment of*
 * a PCLXL font header because it can theoretically be split into
 * multiple segments.
 *
 * Therefore it must extent any existing font header data array
 * being careful to preserve the existing contents
 * to be long enough to also hold this next segment
 *
 * It must then attempt to read this next segment onto the end
 * of the existing data.
 *
 * If the read of this font header data segment
 * then we need to ensure that *someone* will clear up
 * the entire PCLXL_FONT_HEADER[_STRUCT]
 * which *could be* done in here.
 */

static Bool
pclxl_read_font_header(PCLXL_CONTEXT     pclxl_context,
                       PCLXL_FONT_HEADER font_header,
                       uint32            font_header_segment_len)
{
  PCLXL_EMBEDDED_READER embedded_reader;
  uint32 new_header_data_len = (font_header->font_header_data_len +
                                font_header_segment_len);

  /*
   * Now here are some interesting questions:
   *
   * 1) We already have the supposed number of bytes in this next font header
   * data segment.  But I think we are also supposed to expect a 2-byte length
   * field at the start of the following data?
   *
   * 2) If there is indeed a data length Which byte-ordering does it use?  The
   * native PCLXL operator stream byte ordering?  Or the font header data
   * ordering?
   */

  if ( !pclxl_stream_embedded_init(pclxl_context,
                                   pclxl_parser_current_stream(pclxl_context->parser_context),
                                   pclxl_context->parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    /*
     * We have failed to read the embedded data length (2) bytes A suitable
     * error message has been logged So we need to return FALSE here The
     * question is: What, if anything do we do with the font_header?
     *
     * pclxl_delete_font_header(font_header);
     * pclxl_context->font_header = NULL;
     */
    return(FALSE);
  }

  if ( font_header_segment_len != pclxl_embedded_length(&embedded_reader) )
  {
    /*
     * We successfully read the embedded data length
     * But the embedded length does not match the attribute supplied length
     */

    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("FontHeader[Segment] length %d does not matched embedded data length %d, found when reading font \"%s\" header",
                         font_header_segment_len, pclxl_embedded_length(&embedded_reader), font_header->font_name));
    /*
     * As usual the question is: What, if anything do we do with the font_header?
     *
     * pclxl_delete_font_header(font_header);
     * pclxl_context->font_header = NULL;
     */

    return FALSE;
  }

  if ( new_header_data_len > font_header->font_header_data_alloc_len )
  {
    /*
     * There isn't enough space to hold the existing font header data (if any)
     * *and* this latest font header data segment
     * So we must allocate some more space,
     * then move the existing data, if any, across to this newly allocated space
     * and then free the originally allocated space
     */

    uint8* new_header_data;

    uint32 new_header_data_alloc_len = font_header->font_header_data_alloc_len;

    while (new_header_data_len > new_header_data_alloc_len ) new_header_data_alloc_len *= 2;

    if ( (new_header_data = mm_alloc(pclxl_context->memory_pool,
                                     new_header_data_alloc_len,
                                     MM_ALLOC_CLASS_PCLXL_FONT_HEADER)) == NULL )
    {
      /* Oops, the allocation failed
       * We must log a memory allocation failure
       * And return FALSE
       */
       /** \todo
       * The question is:
       * What do we do, if anything, with the existing font header data?
       * Do we free it and possibly the whole font header structure here?
       * Or do we leave it for someone else to do later?
       */

      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_TEXT,
                                 PCLXL_INSUFFICIENT_MEMORY,
                                 ("Failed to allocate space to hold (additional) font header data (segment) of %d bytes  for font name \"%s\"",
                                  font_header_segment_len,
                                  font_header->font_name));

      /** \todo If we decide to do the deletion here:
       * pclxl_delete_font_header(font_header);
       * pclxl_context->font_header = NULL;
       */

      return FALSE;
    }

#ifdef DEBUG_BUILD

    HqMemSet8(new_header_data, 0xff, new_header_data_alloc_len);

#endif /* DEBUG_BUILD */

    if ( (font_header->font_header_data) &&
         (font_header->font_header_data_len > 0) )
    {
      (void) memcpy(new_header_data,
                    font_header->font_header_data,
                    font_header->font_header_data_len);

      if ( font_header->font_header_data_alloc_len > PCLXL_INITIAL_FONT_HEADER_BUF_LEN )
      {
        mm_free(font_header->pclxl_context->memory_pool,
                font_header->font_header_data,
                font_header->font_header_data_alloc_len);
      }
    }

    /*
     * Ok, lets point the font header at this newly allocated space
     * and record the allocation length.
     *
     * Note that this allocated space may already contain
     * some existing data, so we must be careful to read
     * the next segment into this space statring at
     * &new_header_data[font_header->font_header_data_len]
     */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Extended font header data buffer for font name \"%s\", font data stream = 0x%08x, font data length = %d, extended font header data buffer length from %d bytes to %d bytes",
                 pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type),
                 font_header->font_data_stream,
                 font_header->font_header_data_len,
                 font_header->font_header_data_alloc_len,
                 new_header_data_alloc_len));

    font_header->font_header_data = new_header_data;

    font_header->font_header_data_alloc_len = new_header_data_alloc_len;

    /*
     * Note that we must not trample on
     * font_header->font_header_data_len yet
     * because this still holds the existing data, if any, length
     */
  }

  /*
   * We can now go ahead and read font_header_segment_len bytes
   * into the font_header_data buffer starting at
   * &font_header->font_header_data[font_header->font_header_data_len]
   */

  HQASSERT((font_header->font_header_data_alloc_len >= (font_header->font_header_data_len + font_header_segment_len)),
           "Allocated space should greater than or at least equal to the existing font header data + this additional segment length");

  if ( !pclxl_embedded_read_bytes(&embedded_reader,
                                  &font_header->font_header_data[font_header->font_header_data_len],
                                  font_header_segment_len) ) {
    /*
     * We have failed to read exactly the segment length bytes An error has been
     * logged and so we will return false The question is: What, if anything do
     * we do with the font_header?
     *
     * pclxl_delete_font_header(font_header);
     * pclxl_context->font_header = NULL;
     */

    return FALSE;
  }

  font_header->font_header_data_len += font_header_segment_len;

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("Read (another) %d bytes of font \"%s\" header. Total length now %d bytes",
               font_header_segment_len,
               pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type),
               font_header->font_header_data_len));

  return TRUE;
}

/*
 * Tag 0x50 ReadFontHeader
 */

Bool
pclxl_op_read_font_header(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define READFONTHEADER_FONT_HEADER_LENGTH (0)
    {PCLXL_AT_FontHeaderLength | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_FONT_HEADER font_header = pclxl_context->font_header;
  uint32 font_header_length;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* FontHeaderLength */
  font_header_length = pclxl_attr_get_uint(match[READFONTHEADER_FONT_HEADER_LENGTH].result);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("ReadFontHeader(FontName = \"%s\", FontHeader[Segment]Length = %d)",
               pclxl_debug_font_name(font_header->font_name, font_header->font_name_len,
                                     font_header->font_name_type),
               font_header_length));

  return(pclxl_read_font_header(pclxl_context, font_header, font_header_length));
}

static Bool
pclxl_define_font(PCLXL_CONTEXT pclxl_context,
                  PCLXL_FONT_HEADER font_header)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  static sw_datum pcl_miscop_define_font_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_define_font_params + 1, 4),

    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_FONT),     /* PFIN "MISCOP" to define a font */
    SW_DATUM_STRING(""),                 /* Font Name (a.k.a. Font "ID" in PCL5-terms */
    SW_DATUM_STRING(""),                 /* Font Header Data (currently supplied as an array of bytes) */
    SW_DATUM_INTEGER(0)                  /* Optional indicator to specify that the font name
                                          * actually consists of 2-byte characters.
                                          * If it is "0" (zero) then this is a 1-byte/uint8* font name
                                          * Any other value means that it is "some known-only-to-PCL{5,X} font name encoding"
                                          * (I.e. it definitely is not a built-in font)
                                          */
                                         /*
                                          * Note that the PFIN UFST Module
                                          * "groks" the font header data
                                          * and thus derives/obtains its own
                                          * view of the font format
                                          */
  };

  enum { /* indices into the above array */
    p_array = 0, p_reason, p_name, p_data, p_nametype
  } ;

  sw_datum* params = pcl_miscop_define_font_params;
  sw_datum* reply = pcl_miscop_define_font_params;

  sw_pfin_result pfin_miscop_result;

  /*
   * The compiler reports signed/unsigned pointer mismatch warnings
   * unless we cast the values into sw_datum union member types
   * The reason for this explicit comment is because the compiler
   * seems to be mis-reporting the line numbers associated with these particular warnings
   */

  params[p_name].value.string = (const char *) font_header->font_name;

  params[p_name].length = font_header->font_name_len;

  params[p_data].value.string = (const char *) font_header->font_header_data;

  params[p_data].length = font_header->font_header_data_len;

  /*
   * We need to supply something that indicates whether
   * this "font name" is to be interpreted as 8-bit or 16-bit
   *
   * "0" (zero) means 8-bit/uint8 name
   * Anything else means 16-bit (or some other even more esoteric encoding)
   */

  params[p_nametype].value.integer =
    (int32) (font_header->font_name_type ^ PCLXL_DT_UByte_Array);

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_DEFINE_FONT) to define font named \"%s\"",
               pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type)));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to find
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to DefineFont(FontName = \"%s\", FontFormat = %d, FontHeaderDataLen = %d)",
                                font_header->font_name,
                                font_header->font_format,
                                font_header->font_header_data_len));

    return FALSE;
  }
  else if ( (reply != NULL) &&
            (reply->type == SW_DATUM_TYPE_INTEGER) )
  {
    /*
     * We have received a non-NULL reply
     * and it contains an integer value
     *
     * This means that PFIN has detected an error in this font header
     */

    int32 error_code = (- reply->value.integer);

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    error_code,
                                    font_header->font_name,
                                    font_header->font_name_len,
                                    ("Failed to DefineFont(FontName = \"%s\", FontFormat = %d, FontHeaderDataLen = %d), pfin_miscop(PCL_DEFINE_FONT) returned %d",
                                     font_header->font_name,
                                     font_header->font_format,
                                     font_header->font_header_data_len,
                                     reply->value.integer));

    return FALSE;
  }
  else
  {
    /*
     * If we have got to here
     * we must have successfully "defined" this font (header)
     */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("DefineFont(FontName = \"%s\", FontFormat = %d, FontHeaderDataLen = %d) apparently successful",
                 pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type),
                 font_header->font_format,
                 font_header->font_header_data_len));

    return TRUE;
  }

  /*NOTREACHED*/
}

/*
 * Tag 0x51 EndFontHeader
 */

#ifdef DEBUG_BUILD
#define PCLXL_FONT_STATS 1
#endif /* DEBUG_BUILD */
#if PCLXL_FONT_STATS

static uint32 max_font_header_bytes = 0;
static uint32 total_font_header_bytes = 0;
static uint32 total_fonts = 0;

#endif

Bool
pclxl_op_end_font_header(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_FONT_HEADER font_header = pclxl_context->font_header;

  Bool define_font_result;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  HQASSERT((font_header != NULL),
           "Should have an open font header to end");
  /** \todo
   *
   * I guess that we now have a complete font header
   * and we are supposed to do something with it.
   *
   * Specifically I believe that we are supposed to
   * pass this entire font header data, together with
   * the font name and font (header) format
   * across to the UFST PFIN module
   *
   * But as far as I currently understand this is not quite finished
   *
   * So we are simply going to discard this whole font header
   */

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("EndFontHeader(FontName = \"%s\", FontHeaderLength %d)",
               pclxl_debug_font_name(font_header->font_name, font_header->font_name_len, font_header->font_name_type),
               font_header->font_header_data_len));

#if PCLXL_FONT_STATS

  {
    uint32 ave_font_header_bytes = 0;

    max_font_header_bytes = (font_header->font_header_data_len > max_font_header_bytes ? font_header->font_header_data_len : max_font_header_bytes);
    total_fonts++;
    total_font_header_bytes += font_header->font_header_data_len;

    ave_font_header_bytes = (total_font_header_bytes / total_fonts);

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Font Header Statistics: %d fonts, max font header bytes = %d, average font header bytes = %d",
                 total_fonts, max_font_header_bytes, ave_font_header_bytes));
  }

#endif

  define_font_result = pclxl_define_font(pclxl_context,
                                         font_header);

  pclxl_delete_font_header(font_header);

  pclxl_context->font_header = NULL;

  probe_end(SW_TRACE_INTERPRET_PCLXL_FONT, pclxl_context);

  return define_font_result;
}

void init_C_globals_pclxlfont(void)
{
#ifdef PCLXL_FONT_STATS
  max_font_header_bytes = 0;
  total_font_header_bytes = 0;
  total_fonts = 0;
#endif
}

/*
 * Since we are likely to encounter many characters/glyphs
 * being defined and these all have varying data lengths,
 * then depending on the arrival order we will keep making calls
 * to extend the char_data buffer.
 *
 * However we can avoid this by allocating a minimum initial buffer size
 * such that we can typically avoid any further need to extend it
 * (whilst none-the-less leaving the facility to do so just in case)
 *
 * Whilst processing the various Quality Logic test files
 * we have empirically found that a buffer of 1178 bytes
 * is sufficient to process the various instantiations of the
 * "Kanji Vert Subs " font
 *
 * However there are many other fonts that use a maximum char data buffer
 * of somewhere between 250 and 298
 *
 * Therefore, given a progressive doubling from 298 to 596 to 1192
 * the Kanji Vertical Substitution will require at most 2 memory "increments"
 * by starting with an initial size of 298
 */

#define PCLXL_INITIAL_CHAR_DATA_BUF_LEN 298

void
pclxl_delete_char_data(PCLXL_CHAR_DATA char_data)
{
  char_data->char_data_stream = NULL;

  if ( (char_data->char_data != NULL) &&
       (char_data->char_data_alloc_len > PCLXL_INITIAL_CHAR_DATA_BUF_LEN) )
  {
    mm_free(char_data->pclxl_context->memory_pool,
            char_data->char_data,
            char_data->char_data_alloc_len);

    char_data->char_data = ((uint8*) char_data) + (sizeof(PCLXL_CHAR_DATA_STRUCT) + char_data->font_name_len + 1);

    char_data->char_data_alloc_len = PCLXL_INITIAL_CHAR_DATA_BUF_LEN;

    char_data->char_data_size = 0;
  }

  mm_free(char_data->pclxl_context->memory_pool,
          char_data,
          (sizeof(PCLXL_CHAR_DATA_STRUCT) +
           char_data->font_name_len + 1 +
           PCLXL_INITIAL_CHAR_DATA_BUF_LEN));
}

static PCLXL_CHAR_DATA
pclxl_new_char_data(PCLXL_CONTEXT        pclxl_context,
                    uint8*               font_name,
                    uint32               font_name_len,
                    PCLXL_TAG            font_name_type,
                    PCLXLSTREAM*         char_data_stream)
{
  PCLXL_CHAR_DATA new_char_data;

  /*
   * For efficiency we are going to allocate enough space
   * for the PCLXL_CHAR_DATA_STRUCT and the font name
   * and the minimum initial char data buffer as a single allocation
   */

  size_t bytes_to_allocate = (sizeof(PCLXL_CHAR_DATA_STRUCT) +
                              font_name_len + 1 +
                              PCLXL_INITIAL_CHAR_DATA_BUF_LEN);


  if ( (new_char_data = mm_alloc(pclxl_context->memory_pool,
                                 bytes_to_allocate,
                                 MM_ALLOC_CLASS_PCLXL_FONT_CHAR)) == NULL )
  {
    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INSUFFICIENT_MEMORY,
                               ("Failed to allocate new PCLXL font [header] structure"));

    return NULL;
  }
  else
  {
    HqMemZero(new_char_data, bytes_to_allocate);

    new_char_data->pclxl_context = pclxl_context;
    new_char_data->font_name = ((uint8*) new_char_data) + (sizeof(PCLXL_CHAR_DATA_STRUCT));
    new_char_data->font_name_len = font_name_len;
    new_char_data->font_name_type = font_name_type;
    new_char_data->char_count = 0;
    new_char_data->char_data = ((uint8*) new_char_data) + (sizeof(PCLXL_CHAR_DATA_STRUCT) + font_name_len + 1);
    new_char_data->char_data_alloc_len = PCLXL_INITIAL_CHAR_DATA_BUF_LEN;
    new_char_data->char_data_size = 0;

#ifdef DEBUG_BUILD

    HqMemSet8(new_char_data->char_data, 0xff, PCLXL_INITIAL_CHAR_DATA_BUF_LEN);

#endif /* DEBUG_BUILD */
  }

  if ( (font_name != NULL) &&
       (font_name_len > 0) )
  {
    /*
     * Note that we *must* use memcpy() to capture the font "name"
     * because in some cases this font "name" is actually binary data
     * So strncpy() might prematurely terminate the copy when it sees a nul byte
     */

    (void) memcpy(new_char_data->font_name, font_name, font_name_len);
  }

  /*
   * But we will still ensure that the extra 1 byte
   * that we allocated to hold this font "name"
   * is filled in with a '\0' (nul) byte
   * So that we can use this as a C string
   */

  new_char_data->font_name[font_name_len] = '\0';

  new_char_data->char_data_stream = char_data_stream;

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("New Char Data for font name \"%s\", data length = %d, allocated buffer length = %d bytes",
               pclxl_debug_font_name(font_name, font_name_len, font_name_type),
               new_char_data->char_data_size,
               new_char_data->char_data_alloc_len));

  return new_char_data;
}

static Bool
pclxl_begin_char_data(PCLXL_PARSER_CONTEXT parser_context,
                      uint8*               font_name,
                      uint32               font_name_len,
                      PCLXL_TAG            font_name_data_type)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( pclxl_context->char_data != NULL )
  {
    /*
     * There is already an open char data
     * This really should not be possible
     * because the BeginChar operator is not valid
     * within the scope of an existing BeginChar/EndChar pair
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_ILLEGAL_OPERATOR_SEQUENCE,
                               ("There is already an open char data for font name \"%s\"",
                                pclxl_context->char_data->font_name));

    return FALSE;
  }

  if ( (pclxl_context->char_data = pclxl_new_char_data(pclxl_context,
                                                       (uint8*) font_name,
                                                       font_name_len,
                                                       font_name_data_type,
                                                       pclxl_parser_current_stream(parser_context))) == NULL )
  {
    /*
     * We have failed to create a new PCLXL_CHAR_DATA
     * A suitable error has been logged so we simply return FALSE here
     */

    return FALSE;
  }
  else
  {
    probe_begin(SW_TRACE_INTERPRET_PCLXL_FONT, pclxl_context);
    return TRUE;
  }
}

/*
 * Tag 0x52 BeginChar
 */

static
PCLXL_ATTR_MATCH font_name_match[2] = {
#define FONTNAME_FONT_NAME    (0)
  {PCLXL_AT_FontName | PCLXL_ATTR_REQUIRED},
  PCLXL_MATCH_END
};

Bool
pclxl_op_begin_char(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint8* font_name;
  uint32 font_name_len;
  PCLXL_TAG font_name_dt;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, font_name_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* FontName - uint16 font names allowed if not in strict mode */
  if ( (font_name_match[FONTNAME_FONT_NAME].result->data_type == PCLXL_DT_UInt16_Array) &&
       pclxl_context->config_params.strict_pclxl_protocol_class ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE,
                        ("Illegal attribute data type - caught uint16 fontname when in strict mode."));
    return(FALSE);
  }
  font_name_dt = font_name_match[FONTNAME_FONT_NAME].result->data_type;
  pclxl_attr_get_byte_len(font_name_match[FONTNAME_FONT_NAME].result, &font_name, &font_name_len);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("BeginChar(FontName=\"%s\")",
               pclxl_debug_font_name(font_name, font_name_len, font_name_dt)));

  return(pclxl_begin_char_data(parser_context, font_name, font_name_len, font_name_dt));
}

static Bool
pclxl_read_char_data(PCLXL_CONTEXT   pclxl_context,
                     PCLXL_CHAR_DATA char_data,
                     int32           char_code,
                     uint32          char_data_size)
{
  PCLXL_EMBEDDED_READER embedded_reader;

  UNUSED_PARAM(int32, char_code);

  if ( !pclxl_stream_embedded_init(pclxl_context,
                                   pclxl_parser_current_stream(pclxl_context->parser_context),
                                   pclxl_context->parser_context->data_source_big_endian,
                                   &embedded_reader) ) {
    /*
     * We have failed to read the embedded data length (2) bytes
     * A suitable error message has been logged
     * So we need to return FALSE here
     * The question is: What, if anything do we do with the char_data?
     *
     * pclxl_delete_char_data(char_data);
     * pclxl_context->char_data = NULL;
     */

    return FALSE;
  }
  if ( char_data_size != pclxl_embedded_length(&embedded_reader) ) {
    /*
     * We successfully read the embedded data length
     * But the embedded length does not match the attribute supplied length
     */

    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_DATA_LENGTH,
                        ("CharDataSize %d does not matched embedded data length %d, found when reading char data for CharCode %d in font name \"%s\"",
                         char_data_size,
                         pclxl_embedded_length(&embedded_reader), char_code,
                         char_data->font_name));

    /*
     * As usual the question is: What, if anything do we do with the char_data?
     *
     * pclxl_delete_char_data(char_data);
     * pclxl_context->char_data = NULL;
     */

    return FALSE;
  }
  if ( char_data_size > char_data->char_data_alloc_len ) {
    /*
     * There isn't enough space to hold this next char data
     * So we must free the existing space (if any)
     * and allocate some more space for this (larger) char data
     *
     * Note that we attempt to reduce the number of subsequent allocations
     * by not just allocating enough space for *this* char
     * but by repeatedly doubling the current size until it is at least
     * big enough to hold this new character.
     *
     * Note also therefore that the initial size must be greater than zero
     */

    uint32 new_alloc_size = char_data->char_data_alloc_len;

    while (new_alloc_size < char_data_size) new_alloc_size *= 2;

    /*
     * Note that we allocated the initial char data buffer
     * as part of the allocation of the char header structure
     * and so we must *not* free this original allocation
     */

    if ( (char_data->char_data_alloc_len > PCLXL_INITIAL_CHAR_DATA_BUF_LEN) &&
         (char_data->char_data != NULL) )
    {
      mm_free(char_data->pclxl_context->memory_pool,
              char_data->char_data,
              char_data->char_data_alloc_len);

      char_data->char_data = NULL;
      char_data->char_data_size = char_data->char_data_alloc_len = 0;
    }

    if ( (char_data->char_data = mm_alloc(pclxl_context->memory_pool,
                                          new_alloc_size,
                                          MM_ALLOC_CLASS_PCLXL_FONT_CHAR)) == NULL ) {
      /* Oops, the allocation failed We must log a memory allocation failure And
       * return FALSE
       */
      /** \todo The question is: What do we do, if anything, with the parent char
       * data Do we free it here? Or do we leave it for someone else to do
       * later?
       */

      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_INSUFFICIENT_MEMORY,
                          ("Failed to allocate space to hold %d byte char data data for char code %d in (soft) font name \"%s\"",
                           char_data_size, char_code, char_data->font_name));

      /** \todo If we decide to do the deletion here:
       * pclxl_delete_char_data(char_data);
       * pclxl_context->char_data = NULL;
       */

      return FALSE;
    }

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Extended char data buffer from %d bytes to %d bytes",
                 char_data->char_data_alloc_len,
                 new_alloc_size));

    char_data->char_data_alloc_len = new_alloc_size;

#if DEBUG_BUILD

    /* Blat with tell tale to detect problems */
    HqMemSet8(char_data->char_data, 0xff, char_data->char_data_alloc_len);

#endif /* DEBUG_BUILD */
  }

  /*
   * The char_data->char_data buffer is (now) big enough
   * to hold this next char data.
   * So we simply go ahead and re-use it
   *
   * But note we are going to presume that the contained char data
   * is empty until we actually successfully read it
   */

  char_data->char_data_size = 0;

  /*
   * We can now go ahead and read char_data_size bytes into the char_data buffer
   */
  if ( !pclxl_embedded_read_bytes(&embedded_reader, char_data->char_data, char_data_size) ) {
    /*
     * We have failed to read exactly the segment length bytes An error has been
     * logged and so we will return false The question is: What, if anything do
     * we do with the font_header?
     *
     * pclxl_delete_char_data(char_data);
     * pclxl_context->char_data = NULL;
     */

    return FALSE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("Read %d bytes of char data for char_code %d in (soft) font \"%s\"",
               char_data_size, char_code,
               pclxl_debug_font_name(char_data->font_name, char_data->font_name_len, char_data->font_name_type)));

  char_data->char_data_size = char_data_size;

  return TRUE;

}

static Bool
pclxl_define_char(PCLXL_CONTEXT   pclxl_context,
                  PCLXL_CHAR_DATA char_data,
                  int32           char_code)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  static sw_datum pcl_miscop_define_glyph_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_define_glyph_params + 1, 5),

    SW_DATUM_INTEGER(PCL_MISCOP_DEFINE_GLYPH),     /* PFIN "MISCOP" to define a glyph */
    SW_DATUM_STRING(""),                 /* Font Name (a.k.a. Font "ID" in PCL5-terms */
    SW_DATUM_INTEGER(0),                 /* Character Code */
    SW_DATUM_STRING(""),                 /* Font Header Data (currently supplied as an array of bytes) */
    SW_DATUM_INTEGER(0)                  /* Optional indicator to specify that the font name
                                          * actually consists of 2-byte characters.
                                          * If it is "0" (zero) then this is a 1-byte/uint8* font name
                                          * Any other value means that it is "some known-only-to-PCL{5,X} font name encoding"
                                          * (I.e. it definitely is not a built-in font)
                                          */
  };

  enum { /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_name, p_code, p_data, p_nametype
  } ;

  sw_datum* params = pcl_miscop_define_glyph_params;
  sw_datum* reply = pcl_miscop_define_glyph_params;

  sw_pfin_result pfin_miscop_result;

  /*
   * The compiler reports signed/unsigned pointer mismatch warnings
   * unless we cast the values into sw_datum union member types
   * The reason for this explicit comment is because the compiler
   * seems to be mis-reporting the line numbers associated with these particular warnings
   */

  params[p_name].value.string = (const char *) char_data->font_name;

  params[p_name].length = char_data->font_name_len;

  params[p_code].value.integer = char_code;

  params[p_data].value.string = (const char *) char_data->char_data;

  params[p_data].length = char_data->char_data_size;

  /*
   * Again we need to resolve 8-bit font names from 16-bit names
   * But I don't know whether to supply a boolean
   *
   * "0" (zero) means 8-bit/uint8 name
   * Anything else means 16-bit (or some other even more esoteric encoding)
   */

  params[p_nametype].value.integer =
    (int32) (char_data->font_name_type ^ PCLXL_DT_UByte_Array);

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_DEFINE_GLYPH) to define char code %d (%d bytes of data) in font name \"%s\"",
               char_code,
               char_data->char_data_size,
               pclxl_debug_font_name(char_data->font_name, char_data->font_name_len, char_data->font_name_type)));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to define the character/glyph
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to DefineGlyph(FontName = \"%s\", CharCode = %d, CharDataLen = %d)",
                                char_data->font_name,
                                char_code,
                                char_data->char_data_size));

    return FALSE;
  }
  else if ( (reply != NULL) &&
            (reply->type == SW_DATUM_TYPE_INTEGER) )
  {
    /*
     * We have received a non-NULL reply
     * and it contains an integer value
     *
     * This means that PFIN has detected an error in this glyph definition
     */

    int32 error_code = (- reply->value.integer);

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    error_code,
                                    char_data->font_name,
                                    char_data->font_name_len,
                                    ("Failed to DefineGlyph(FontName = \"%s\", CharCode = %d, CharDataLen = %d), pfin_miscop(PCL_MISCOP_DEFINE_GLYPH) returned %d",
                                     char_data->font_name,
                                     char_code,
                                     char_data->char_data_size,
                                     reply->value.integer));

    return FALSE;
  }
  else
  {
    /*
     * If we have got to here
     * we must have successfully "defined" this character/glyph
     */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("DefineGlyph(FontName = \"%s\", CharCode = %d, CharDataLen = %d) apparently successful",
                 pclxl_debug_font_name(char_data->font_name, char_data->font_name_len, char_data->font_name_type),
                 char_code,
                 char_data->char_data_size));

    return TRUE;
  }

  /*NOTREACHED*/
}

/*
 * Tag 0x53 ReadChar
 */

#if PCLXL_FONT_STATS

static uint32 max_char_data_bytes = 0;
static uint32 total_char_data_bytes = 0;
static uint32 total_chars = 0;

#endif

Bool
pclxl_op_read_char(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[3] = {
#define READCHAR_CHAR_CODE      (0)
    {PCLXL_AT_CharCode | PCLXL_ATTR_REQUIRED},
#define READCHAR_CHAR_DATA_SIZE (1)
    {PCLXL_AT_CharDataSize | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_CHAR_DATA char_data = pclxl_context->char_data;

  int32 char_code;
  uint32 char_data_size;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharCode */
  char_code = pclxl_attr_get_int(match[READCHAR_CHAR_CODE].result);
  /* CharDataSize */
  char_data_size = pclxl_attr_get_uint(match[READCHAR_CHAR_DATA_SIZE].result);

  HQASSERT((char_data != NULL),
           "Character data has not been opened prior to read");

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("ReadChar(FontName = \"%s\", CharCode = %d CharDataSize = %d)",
               pclxl_debug_font_name(char_data->font_name, char_data->font_name_len,
                                     char_data->font_name_type),
               char_code, char_data_size));

#if PCLXL_FONT_STATS

  max_char_data_bytes = max(max_char_data_bytes, char_data_size);
  total_chars++;
  total_char_data_bytes += char_data_size;

#endif

  if ( !pclxl_read_char_data(pclxl_context, char_data, char_code, char_data_size) ) {
    /* We have failed to read this font character (data) */
    return FALSE;
  }
  if ( !pclxl_define_char(pclxl_context, char_data, char_code) ) {
    /* We have failed to "define" this character with the UFST PFIN module */
    return FALSE;
  }

  /*
   * Having read and defined this character/glyph we can "throw the char data
   * away"
   *
   * But we are going to do this by just setting the char_data->char_data_size
   * back to zero
   *
   * We are *not* going to free the allocated char data buffer because it is
   * *very likely that the same-sized buffer will be needed to read the next
   * character in this user-defined/downloaded/"soft" font
   *
   * So we only actually free the buffer when we see the EndChar operator
   */
  char_data->char_data_size = 0;

  return TRUE;
}

/*
 * Tag 0x54 EndChar
 */

Bool
pclxl_op_end_char(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_CHAR_DATA char_data = pclxl_context->char_data;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  HQASSERT((char_data != NULL),
           "There should be an open char data to end.");

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("EndChar(FontName = \"%s\")",
               pclxl_debug_font_name(char_data->font_name, char_data->font_name_len, char_data->font_name_type)));

#if PCLXL_FONT_STATS

  {
    uint32 ave_char_data_bytes = (total_char_data_bytes / total_chars);

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Char Data Statistics: %d chars, max char data bytes = %d, average char data bytes = %d",
                 total_chars, max_char_data_bytes, ave_char_data_bytes));
  }

#endif

  /*
   * We have now finished defining all the characters for this font
   * (One character per PCLXL "ReadChar")
   * So we can now discard this PCLXL_CHAR_DATA structure
   */

  probe_end(SW_TRACE_INTERPRET_PCLXL_FONT, pclxl_context);
  pclxl_delete_char_data(char_data);

  pclxl_context->char_data = NULL;

  return TRUE;
}

static Bool
pclxl_remove_font(PCLXL_CONTEXT pclxl_context,
                  uint8*        font_name,
                  uint32        font_name_len,
                  PCLXL_TAG     font_name_data_type)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  static sw_datum pclxl_miscop_remove_font_params[] =
  {
    SW_DATUM_ARRAY(pclxl_miscop_remove_font_params + 1, 5),

    SW_DATUM_INTEGER(PCL_MISCOP_FONT),   /* PFIN "MISCOP" to perform some "control" operation on a font */
    SW_DATUM_INTEGER(2),                 /* and the "control" this time is remove font */
    SW_DATUM_STRING(""),                 /* Font Name (a.k.a. Font "ID" in PCL5-terms */
    SW_DATUM_STRING(""),                 /* Font to copy (or proxy). Not applicable to XL. */
    SW_DATUM_INTEGER(0)                  /* Optional indicator to specify that the font name
                                          * actually consists of 2-byte characters.
                                          * If it is "0" (zero) then this is a 1-byte/uint8* font name
                                          * Any other value means that it is "some known-only-to-PCL{5,X} font name encoding"
                                          * (I.e. it definitely is not a built-in font)
                                          */
  };

  enum { /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_action, p_name, p_copy, p_nametype
  } ;

  sw_datum* params = pclxl_miscop_remove_font_params;
  sw_datum* reply = pclxl_miscop_remove_font_params;

  sw_pfin_result pfin_miscop_result;

  /*
   * The compiler reports signed/unsigned pointer mismatch warnings
   * unless we cast the values into sw_datum union member types
   * The reason for this explicit comment is because the compiler
   * seems to be mis-reporting the line numbers associated with these particular warnings
   */

  params[p_name].value.string = (const char *) font_name;

  params[p_name].length = font_name_len;

  /*
   * We need to supply something that indicates whether
   * this "font name" is to be interpreted as 8-bit or 16-bit
   *
   * "0" (zero) means 8-bit/uint8 name
   * Anything else means 16-bit (or some other even more esoteric encoding)
   */

  params[p_nametype].value.integer =
    (int32) (font_name_data_type ^ PCLXL_DT_UByte_Array);

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_FONT(RemoveFont)) to remove font named \"%s\"",
               pclxl_debug_font_name(font_name, font_name_len, font_name_data_type)));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS ) {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to remove the font
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to remove font \"%s\"",
                                font_name));

    return FALSE;
  } else if ( reply != NULL &&
              reply->type == SW_DATUM_TYPE_INTEGER ) {
    /*
     * We have received a non-NULL reply
     * and it contains an integer value
     *
     * This means that PFIN has detected an error when trying to remove this font
     *
     * This is probably because it is an "internal" font that cannot be removed
     * or because it is a user-defined (a.k.a. "soft") font that has not been defined
     */

    int32 error_code = (- reply->value.integer);

    switch (error_code) {
    case PCLXL_UNDEFINED_FONT_NOT_REMOVED:
    case PCLXL_INTERNAL_FONT_NOT_REMOVED:
    case PCLXL_MASS_STORAGE_FONT_NOT_REMOVED:

      (void) pclxl_resource_not_removed_warning(__FILE__, __LINE__,
                                                pclxl_context,
                                                PCLXL_SS_TEXT,
                                                error_code,
                                                font_name,
                                                font_name_len);
      return TRUE;
    }

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    error_code,
                                    font_name,
                                    font_name_len,
                                    ("Failed to remove font \"%s\" pfin_miscop(PCL_MISCOP_FONT(RemoveFont)) returned %d)",
                                     font_name,
                                     reply->value.integer));

    return FALSE;
  } else {
    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Removed font \"%s\"",
                 pclxl_debug_font_name(font_name, font_name_len, font_name_data_type)));

    return TRUE;
  }
}

Bool
pclxl_remove_soft_fonts(PCLXL_CONTEXT pclxl_context,
                        Bool          include_permanent_soft_fonts)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;

  static sw_datum pclxl_miscop_remove_fonts_params[] =
  {
    SW_DATUM_ARRAY(pclxl_miscop_remove_fonts_params + 1, 2),

    SW_DATUM_INTEGER(PCL_MISCOP_FONT),   /* PFIN "MISCOP" to perform some "control" operation on a font */
    SW_DATUM_INTEGER(0),                 /* "0" means remove/delete all soft fonts
                                          * "1" means remove/delete all "temporary" soft fonts
                                          * The "temporary"-ness of a font only really affects PCL5 soft fonts
                                          * but we might need to consider this if for instance
                                          * a PCL5 "pass-through" defines a permanent soft font
                                          * and then a PCL5 job, *after* this PCLXL job ends,
                                          * then expects to be able to access this permanent soft font
                                          */
  };

  enum { /* parameter indices (into the above array) */
    p_array = 0, p_reason, p_action
  } ;

  sw_datum* params = pclxl_miscop_remove_fonts_params;
  sw_datum* reply = pclxl_miscop_remove_fonts_params;

  sw_pfin_result pfin_miscop_result;

#ifdef DEBUG_BUILD

  uint8* remove_font_op = (include_permanent_soft_fonts ?
                           (uint8*) "RemoveAllPermanentSoftFonts" :
                           (uint8*) "RemoveAllTemporarySoftFonts");

#endif

  params[p_action].value.integer = (include_permanent_soft_fonts ? 0 : 1);

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_FONT(%s))",
               remove_font_op));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to remove the font
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to %s",
                                remove_font_op));

    return FALSE;
  }
  else if ( (reply != NULL) &&
            (reply->type == SW_DATUM_TYPE_INTEGER) )
  {
    /*
     * We have received a non-NULL reply
     * and it contains an integer value
     *
     * This means that PFIN has detected an error when trying to remove this font
     *
     * This is probably because it is an "internal" font that cannot be removed
     * or because it is a user-defined (a.k.a. "soft") font that has not been defined
     */

    int32 error_code = (- reply->value.integer);

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               error_code,
                               ("Failed to %s. pfin_miscop(PCL_MISCOP_FONT(%s)) returned %d)",
                                remove_font_op,
                                remove_font_op,
                                reply->value.integer));

    return FALSE;
  }
  else
  {
    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Successfully %s",
                 remove_font_op));

#if PCLXL_FONT_STATS

    {
      uint32 ave_font_header_bytes = (total_fonts ? (total_font_header_bytes / total_fonts) : 0);
      uint32 ave_char_data_bytes = (total_chars ? (total_char_data_bytes / total_chars) : 0);

      PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                  ("Soft Font Statistics: %d fonts, %d characters, average font header = %d bytes, average char data = %d bytes, max font header = %d bytes, max char data = %d bytes",
                   total_fonts, total_chars,
                   ave_font_header_bytes, ave_char_data_bytes,
                   max_font_header_bytes, max_char_data_bytes));
    }

#endif

    return TRUE;
  }

  /*NOTREACHED*/
}

/*
 * Tag 0x55 RemoveFont
 */

Bool
pclxl_op_remove_font(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint8* font_name;
  uint32 font_name_len;
  PCLXL_TAG font_name_dt;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, font_name_match,
                             pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* FontName - uint16 font names allowed if not in strict mode */
  if ( (font_name_match[FONTNAME_FONT_NAME].result->data_type == PCLXL_DT_UInt16_Array) &&
       pclxl_context->config_params.strict_pclxl_protocol_class ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE,
                        ("Illegal attribute data type - caught uint16 fontname when in strict mode."));
    return(FALSE);
  }
  font_name_dt = font_name_match[FONTNAME_FONT_NAME].result->data_type;
  pclxl_attr_get_byte_len(font_name_match[FONTNAME_FONT_NAME].result, &font_name, &font_name_len);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("RemoveFont(FontName = \"%s\")",
               pclxl_debug_font_name(font_name, font_name_len, font_name_dt)));

  return(pclxl_remove_font(pclxl_context, font_name, font_name_len, font_name_dt));
}

/*
 * Tag 0x56 SetCharAttributes
 */

Bool
pclxl_op_set_char_attributes(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARATTR_WRITING_MODE    (0)
    {PCLXL_AT_WritingMode | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_char_attribute_values[] = {
    PCLXL_eVertical,
    PCLXL_eHorizontal,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_WritingMode writing_mode;

#ifdef DEBUG_BUILD
  static uint8* writing_mode_strings[] = {
    (uint8*) "Horizontal",
    (uint8*) "Vertical"
  };
#endif

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* WritingMode */
  if ( !pclxl_attr_match_enumeration(match[SETCHARATTR_WRITING_MODE].result,
                                     allowed_char_attribute_values,
                                     &writing_mode, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCharAttributes(WritingMode = %s)",
               writing_mode_strings[writing_mode]));

  return(pclxl_set_char_attributes(pclxl_context, pclxl_context->graphics_state, writing_mode));
}

/*
 * Tag 0x64 SetCharAngle
 */

Bool
pclxl_op_set_char_angle(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARANGLE_CHAR_ANGLE   (0)
    {PCLXL_AT_CharAngle | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal char_angle;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharAngle */
  char_angle = pclxl_attr_get_real(match[SETCHARANGLE_CHAR_ANGLE].result);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetCharAngle(%f)", char_angle));

#define CHARANGLE_MIN (-360)
#define CHARANGLE_MAX (360)
  if ( (char_angle < CHARANGLE_MIN) || (char_angle > CHARANGLE_MAX) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("CharAngle %f must be in the range -360.0 to +360.0",
                         char_angle));
    return(FALSE);
  }

  return(pclxl_set_char_angle(pclxl_context->graphics_state, char_angle));
}

/*
 * Tag 0x65 SetCharScale
 */

Bool
pclxl_op_set_char_scale(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARSCALE_CHAR_SCALE   (0)
    {PCLXL_AT_CharScale | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_XY char_scale;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharScale */
  pclxl_attr_get_real_xy(match[SETCHARSCALE_CHAR_SCALE].result, &char_scale);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCharScale([ %f, %f])", char_scale.x, char_scale.y));

#define MIN_SCALE (-32768)
#define MAX_SCALE (32767)
  /** \todo need to exclude 0.0? */
  if ( (char_scale.x < MIN_SCALE) || (char_scale.x > MAX_SCALE) ||
       (char_scale.y < MIN_SCALE) || (char_scale.y > MAX_SCALE) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("CharScale values [%f, %f] must both be in the range -32768.0 to +32767.0",
                         char_scale.x, char_scale.y));
    return FALSE;
  }

  return(pclxl_set_char_scale(pclxl_context->graphics_state, &char_scale));
}

/*
 * Tag 0x66 SetCharShear
 */

Bool
pclxl_op_set_char_shear(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARSHEAR_CHAR_SHEAR   (0)
    {PCLXL_AT_CharShear | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal_XY char_shear;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharShear */
  pclxl_attr_get_real_xy(match[SETCHARSHEAR_CHAR_SHEAR].result, &char_shear);

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("SetCharShear([ %f, %f])", char_shear.x, char_shear.y));

#define MIN_SHEAR (-32768)
#define MAX_SHEAR (32767)
  if ( (char_shear.x < MIN_SHEAR) || (char_shear.x > MAX_SHEAR) ||
       (char_shear.y < MIN_SHEAR) || (char_shear.y > MAX_SHEAR) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("CharShear values [%f, %f] must both be in the range -32768.0 to +32767.0",
                         char_shear.x, char_shear.y));
    return(FALSE);
  }

  return(pclxl_set_char_shear(pclxl_context->graphics_state, &char_shear));
}

/**
 * \brief pclxl_record_font_details() captures the PCLXL font details
 * and where available the corresponding underlying Postscript font details
 * into the current graphics state current font details.
 *
 * It also sets a couple of values to record the current state
 * of the PCLXL and Postscript fonts
 *
 * It is anticipated that these details will be supplied
 * to pclxl_ps_select_font():
 *
 * a) When a PCLXL SetFont operator is handled
 *
 * b) When a PCLXL Text operator is requested to
 *    "plot" some characters using the current font.
 *
 * The call to pclxl_ps_select_font() must be done in *both* places
 * because there are various other PCLXL operators that
 * render the current Postscript font (typically the font matrix) invalid
 */

void
pclxl_record_font_details(PCLXL_GRAPHICS_STATE graphics_state,
                          uint8                pclxl_font_state,
                          uint8                pclxl_font_type,
                          uint8*               pclxl_font_name,
                          uint8                pclxl_font_name_len,
                          PCLXL_SysVal         pclxl_char_size,
                          PCLXL_FontID         font_id,
                          uint32               symbol_set,
                          uint8*               ps_font_name,
                          uint8                ps_font_name_len,
                          PCLXL_SysVal         ps_font_point_size,
                          Bool                 ps_font_is_bitmapped)
{
  PCLXL_FONT_DETAILS font_details;

  HQASSERT((graphics_state != NULL), "Cannot store (current) PCLXL/Postscript"
                                     "font name/point size beneath a NULL"
                                     "graphics state");

  font_details = &graphics_state->char_details.current_font;

  HQASSERT((pclxl_font_name_len < (sizeof(font_details->pclxl_font_name) - 1)),
           "PCLXL font name too long");

  HQASSERT((ps_font_name_len < (sizeof(font_details->ps_font_name) - 1)),
           "Postscript font name too long");

  font_details->pclxl_font_state = pclxl_font_state;
  font_details->pclxl_font_type = pclxl_font_type;
  font_details->pclxl_char_size = pclxl_char_size;
  font_details->font_id = font_id;
  font_details->symbol_set = symbol_set;
  font_details->ps_font_point_size = ps_font_point_size;
  font_details->ps_font_is_bitmapped = ps_font_is_bitmapped;

  if ( pclxl_font_name && pclxl_font_name_len )
  {
    (void) memcpy(font_details->pclxl_font_name,
                  pclxl_font_name,
                  font_details->pclxl_font_name_len = pclxl_font_name_len);

    font_details->pclxl_font_name[font_details->pclxl_font_name_len] = '\0';

    /*
     * If we seem to think that we have no current font
     * then we bump the state up to say that at least the PCLXL font is set
     */
    if ( font_details->pclxl_font_state < PCLXL_FS_PCLXL_FONT_SET )
      font_details->pclxl_font_state = PCLXL_FS_PCLXL_FONT_SET;
  }
  else
  {
    HqMemZero(font_details->pclxl_font_name,
              sizeof(font_details->pclxl_font_name));

    font_details->pclxl_font_name_len = 0;
    font_details->pclxl_font_state = PCLXL_FS_NO_CURRENT_FONT;
    font_details->pclxl_font_type = PCLXL_FT_NO_CURRENT_FONT;
  }

  if ( ps_font_name && ps_font_name_len )
  {
    (void) memcpy(font_details->ps_font_name,
                  ps_font_name,
                  font_details->ps_font_name_len = ps_font_name_len);

    font_details->ps_font_name[font_details->ps_font_name_len] = '\0';

    /*
     * Note that we make no assumptions about this being
     * a *valid* Postscript font at this stage.
     *
     * So we must explicitly not attempt to push this font state
     * upto PCLXL_FS_POSTSCRIPT_FONT_SELECTED *here*
     *
     * Instead we only set it to PCLXL_FS_POSTSCRIPT_FONT_SELECTED
     * in pclxl_ps_set_font() iff the Postscript "selectfont" operation
     * succeeds
     */
  }
  else
  {
    /*
     * We mark this font as not being a valid/selected Postscript font
     * whenever we change *anything* about either this particular font (name)
     * including the char scale/shear/rotation/boldness
     *
     * Then we can re-look-up and re-selectfont whenever we detect that this
     * has become necessary
     */
    HqMemZero(font_details->ps_font_name,
              sizeof(font_details->ps_font_name));

    font_details->ps_font_name_len = 0;

    if ( font_details->pclxl_font_state > PCLXL_FS_PFIN_FONT_SELECTED )
      font_details->pclxl_font_state = PCLXL_FS_PFIN_FONT_SELECTED;
  }

#ifdef DEBUG_BUILD
  {
    static char* font_state_strings[] =
    {
      "No current font",
      "PCLXL font set",
      "PFIN font selected"
    };

    static char* font_type_strings[] =
    {
      "Unknown font",
      "PCLXL font (8-bit character font name)",
      "PCLXL font (16-bit character font name)",
      "PCL5 font selection by criteria"
    };

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Font state = %s, type = %s, name = \"%s\" (length = %d bytes), ID = %d => Postscript font \"%s\" (point size = %f)",
                 font_state_strings[font_details->pclxl_font_state],
                 font_type_strings[font_details->pclxl_font_type],
                 pclxl_debug_font_name(font_details->pclxl_font_name, font_details->pclxl_font_name_len, font_details->pclxl_font_type),
                 font_details->pclxl_font_name_len,
                 font_details->font_id,
                 font_details->ps_font_name,
                 font_details->ps_font_point_size));

    return;
  }
#endif
}

/**
 * \brief pcl5_record_font_details() captures the PCLXL font details
 * and where available the corresponding underlying Postscript font details
 * into the current graphics state current font details.
 * This includes a complete set of PCL5 font selection criteria
 * See pclxl_record_font_details for more info.
 */
void
pcl5_record_font_details(PCLXL_GRAPHICS_STATE graphics_state,
                         uint8                pclxl_font_state,
                         uint8                pclxl_font_type,
                         uint8*               pclxl_font_name,
                         uint8                pclxl_font_name_len,
                         PCLXL_SysVal         pclxl_char_size,
                         PCLXL_FontID         font_id,
                         uint32               symbol_set,
                         int32                spacing,
                         float                pitch,
                         float                height,
                         int32                style,
                         int32                weight,
                         int32                typeface,
                         uint8*               ps_font_name,
                         uint8                ps_font_name_len,
                         PCLXL_SysVal         ps_font_point_size,
                         Bool                 ps_font_is_bitmapped)
{
  PCLXL_FONT_DETAILS font_details;
  PCL5_FONT_SELECTION_CRITERIA* pcl5_font_selection_criteria;

  HQASSERT((graphics_state != NULL), "Cannot store (current) PCLXL/Postscript font name/point size beneath a NULL graphics state");

  font_details = &graphics_state->char_details.current_font;
  pcl5_font_selection_criteria = &font_details->pcl5_font_selection_criteria;

  HQASSERT((pclxl_font_name_len < (sizeof(font_details->pclxl_font_name) - 1)),
           "PCLXL font name too long");

  HQASSERT((ps_font_name_len < (sizeof(font_details->ps_font_name) - 1)),
           "Postscript font name too long");

  /* Record the PCL5 fontselection criteria */
  pcl5_font_selection_criteria->symbol_set = symbol_set;
  pcl5_font_selection_criteria->spacing = spacing;
  pcl5_font_selection_criteria->pitch = pitch;
  pcl5_font_selection_criteria->height = height;
  pcl5_font_selection_criteria->style = style;
  pcl5_font_selection_criteria->weight = weight;
  pcl5_font_selection_criteria->typeface = typeface;
  pcl5_font_selection_criteria->pcl5_informed = FALSE;

  /* Record all the usual PCLXL fontselection details */
  (void) pclxl_record_font_details(graphics_state,
                                   pclxl_font_state,
                                   pclxl_font_type,
                                   pclxl_font_name,
                                   pclxl_font_name_len,
                                   pclxl_char_size,
                                   font_id,
                                   symbol_set,
                                   ps_font_name,
                                   ps_font_name_len,
                                   ps_font_point_size,
                                   ps_font_is_bitmapped) ;
}

/**
 * \brief pclxl_set_pclxl_font() takes a PCLXL font name (an exactly 16-character long string),
 * a desired character size (in user-units) and a "symbol set" ID
 *
 * \todo it might be interesting and useful for PCL5 PassThrough to support
 * font names that consist entirely of digits (with trailing white space)
 * as FontIDs rather than font names as this could make communicating a font change/font selection
 * between PCLXL and PCL5 more reliable/guaranteed to produce the same resultant font
 *
 * It validates that both the char_size and symbol_set are greater than zero and
 * converts the char_size into the equivalent point-size
 * and then uses the "PFIN" interface to look up the nearest equivalent
 * to this font (whose actual point size may not be exactly the requested size).
 */

Bool
pclxl_set_pclxl_font(PCLXL_PARSER_CONTEXT parser_context,
                     uint8*               pclxl_font_name,
                     uint32               pclxl_font_name_len,
                     PCLXL_TAG            pclxl_font_name_type,
                     PCLXL_SysVal         char_size,
                     uint32               symbol_set,
                     Bool                 outline_char_path)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;

  PCLXL_SysVal point_size =
    ((char_size * pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit)) / non_gs_state->units_per_measure.res_y);

  static sw_datum pcl_miscop_select_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_select_params + 1, 5),

    SW_DATUM_INTEGER(PCL_MISCOP_SELECT),  /* PFIN "MISCOP" to select a PCLXL "soft" font by name */
    SW_DATUM_STRING(""),                  /* PCLXL "font name" which may contain 8-bit or 16-bit characters */
    SW_DATUM_INTEGER(0),                  /* Mandatory 8-bit/16-bit font name selector
                                           * If it is 0 (zero) then the font name uses 8-bit unsigned chars.
                                           * If it is any non-zero value then it means "some known-only-to-PCL{5,X} font name encoding"
                                           */
    SW_DATUM_INTEGER(0),                  /* symbolset */
    SW_DATUM_INTEGER(0),                  /* Char Boldness */
    SW_DATUM_INTEGER(0)                   /* font height in  1/100th pts */
  };

  sw_datum* select_params = pcl_miscop_select_params;
  sw_datum* select_reply = pcl_miscop_select_params;

  static sw_datum pcl_miscop_xl_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_xl_params + 1, 7),

    SW_DATUM_INTEGER(PCL_MISCOP_XL),     /* PFIN "MISCOP" to select a PCLXL "built-in" font by name */
    SW_DATUM_STRING(""),                 /* PCLXL "font name", typically  but not exclusively 16-characters
                                          * which may be experssed as 16-bit characters */
    SW_DATUM_INTEGER(0),                 /* Optional indicator to specify that the font name
                                          * actually consists of 2-byte characters.
                                          * If it is 0 (zero) then this is a 1-byte/uint8* font name
                                          * Any other value means that it is "some known-only-to-PCL{5,X} font name encoding"
                                          * (I.e. it definitely is not a built-in font)
                                          */
    SW_DATUM_INTEGER(0),                 /* symbolset */
    SW_DATUM_INTEGER(0),                 /* Char Boldness */
    SW_DATUM_FLOAT(SW_DATUM_0_0F),       /* font height (pts) */
    SW_DATUM_INTEGER(0)                  /* Print resolution, not used from XL */
  };

  enum { /* parameter indices (into both above arrays) */
    p_array = 0, p_reason, p_name, p_nametype, p_ss, p_bold, p_height, p_print_resolution
  } ;

  enum {  /* result indices from PCL_MISCOP_SELECT */
    r_name = 0, r_size, r_hmi, r_ss, r_space, r_pitch, r_height, r_style,
    r_weight, r_font, r_sstype, r_offset, r_thick, r_ref, r_bitmap
  } ;

  enum {  /* result indices from PCL_MISCOP_XL */
    x_name = 0, x_size
  } ;

  sw_datum* xl_params = pcl_miscop_xl_params;
  sw_datum* xl_reply = pcl_miscop_xl_params;

  sw_pfin_result pfin_miscop_result;

  /*
   * We also need to supply the "char (faux) boldness value"
   * which is the PCLXL char boldness in the range 0.0 to 1.0
   * multiplied by 32768 and then rounded to the nearest integer
   *
   * However the PCL_MISCOP_XL parameter list does not yet support this
   */

  int char_boldness = (outline_char_path ?
                       0 :
                       (int) ((char_details->char_boldness * 32768) + 0.5));

  /*
   * The compiler reports signed/unsigned pointer mismatch warnings
   * unless we cast the values into sw_datum union member types
   * The reason for this explicit comment is because the compiler
   * seems to be mis-reporting the line numbers associated with these particular warnings
   */

  select_params[p_name].value.string = (const char *) pclxl_font_name;
  select_params[p_name].length = pclxl_font_name_len;

  select_params[p_nametype].value.integer =
    (int32) (pclxl_font_name_type ^ PCLXL_DT_UByte_Array);

  select_params[p_ss].value.integer = (int) symbol_set;
  select_params[p_bold].value.integer = char_boldness;
  select_params[p_height].value.integer = (int) (100 * point_size + 0.5);

  xl_params[p_name].value.string = (const char *) pclxl_font_name;
  xl_params[p_name].length = pclxl_font_name_len;

  xl_params[p_nametype].value.integer =
    (int32) (pclxl_font_name_type ^ PCLXL_DT_UByte_Array);

  xl_params[p_ss].value.integer = (int) symbol_set;
  xl_params[p_bold].value.integer = (int) char_boldness;
  xl_params[p_height].value.real = (float) point_size;
  xl_params[p_print_resolution].value.integer = 0; /* Not used from XL. */

  /*
   * Ok, we have a (hopefully temporary) problem here:
   *
   * The PFIN UFST5 "Interface" currently (erroneously) provides
   * no less than two separate interfaces for selecting PCLXL fonts
   *
   * PCL_MISCOP_SELECT looks for/finds so-called "soft" fonts *only*
   * PCL_MISCOP_XL looks for/finds the so-called "built-in" fonts *only*
   *
   * We always try the PCL_MISCOP_SELECT first, then the PCL_MISCOP_XL
   *
   * Hopefully we will eventually always find the font via
   * the PCL_MISCOP_SELECT interface so we can then delete the redundant
   * second call
   */

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_SELECT) to select PCLXL font name \"%s\"",
               pclxl_debug_font_name(pclxl_font_name, pclxl_font_name_len, pclxl_font_name_type)));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &select_reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to find
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to SetFont([PCLXL] FontName = \"%s\", char_size = %f (point_size = %f), SymbolSet = %d",
                                (char*) pclxl_font_name,
                                char_size,
                                point_size,
                                symbol_set));

    return FALSE;
  }
  else if ( select_reply != NULL )
  {
    HQASSERT((select_reply->type == SW_DATUM_TYPE_ARRAY && select_reply->length >= 15),
             "Unexpected reply from pfin_miscop(PCL_MISCOP_SELECT)");
    HQASSERT((select_reply->owner == 0), "Unexpected reply owner");

    select_reply = (sw_datum*) select_reply->value.opaque;

    HQASSERT((select_reply[r_name  ].type == SW_DATUM_TYPE_STRING &&  /* PS font name (uint8 array) */
              select_reply[r_size  ].type == SW_DATUM_TYPE_FLOAT &&   /* font size (in "points") */
              select_reply[r_space ].type == SW_DATUM_TYPE_INTEGER &&   /* spacing */
              select_reply[r_ref   ].type == SW_DATUM_TYPE_INTEGER && /* font ref */
              select_reply[r_bitmap].type == SW_DATUM_TYPE_BOOLEAN), /* is bitmapped */
             "Unexpected reply array from pfin_miscop(PCL_MISCOP_SELECT)") ;

    (void) pclxl_record_font_details(graphics_state,
                                     PCLXL_FS_PFIN_FONT_SELECTED,
                                     (pclxl_font_name_type == PCLXL_DT_UByte_Array ?
                                      PCLXL_FT_8_BIT_FONT_NAME :
                                      PCLXL_FT_16_BIT_FONT_NAME),
                                     (uint8*) pclxl_font_name,
                                     (uint8) pclxl_font_name_len,
                                     char_size,
                                     select_reply[r_ref].value.integer,
                                     symbol_set,
                                     (uint8*) select_reply[r_name].value.string,
                                     (uint8) select_reply[r_name].length,
                                     select_reply[r_size].value.real,
                                     select_reply[r_bitmap].value.boolean);

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("FontName = \"%s\", SymbolSet = %d, CharSize = %f CharBoldness = %f (point size = %f) -> Postscript Font \"%s\" PointSize = %f (which may or may not be a valid Postscript font)",
                 pclxl_debug_font_name(pclxl_font_name, pclxl_font_name_len, pclxl_font_name_type),
                 symbol_set,
                 char_size,
                 graphics_state->char_details.char_boldness,
                 point_size,
                 graphics_state->char_details.current_font.ps_font_name,
                 graphics_state->char_details.current_font.ps_font_point_size));

    return TRUE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_XL) to select PCLXL font name \"%s\"",
               pclxl_debug_font_name(pclxl_font_name, pclxl_font_name_len, pclxl_font_name_type)));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &xl_reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to find
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to SetFont([PCLXL] FontName = \"%s\", char_size = %f (point_size = %f), SymbolSet = %d",
                                (char*) pclxl_font_name,
                                char_size,
                                point_size,
                                symbol_set));

    return FALSE;
  }
  else if ( xl_reply != NULL )
  {
    HQASSERT((xl_reply->type == SW_DATUM_TYPE_ARRAY && xl_reply->length == 2),
             "Unexpected reply from pfin_miscop(PCL_MISCOP_XL)");
    HQASSERT((xl_reply->owner == 0), "Unexpected reply owner");

    xl_reply = (sw_datum*) xl_reply->value.opaque;

    HQASSERT((xl_reply[x_name].type == SW_DATUM_TYPE_STRING &&   /* PS font name (uint8 array) */
              xl_reply[x_size].type == SW_DATUM_TYPE_FLOAT),     /* font size (in "points") */
             "Unexpected reply array from pfin_miscop(PCL_MISCOP_XL)") ;

    (void) pclxl_record_font_details(graphics_state,
                                     PCLXL_FS_PFIN_FONT_SELECTED,
                                     (pclxl_font_name_type == PCLXL_DT_UByte_Array ?
                                      PCLXL_FT_8_BIT_FONT_NAME :
                                      PCLXL_FT_16_BIT_FONT_NAME),
                                     (uint8*) pclxl_font_name,
                                     (uint8) pclxl_font_name_len,
                                     char_size,
                                     -2,               /* FontID not returned by PCL_MISCOP_XL */
                                     symbol_set,
                                     (uint8*) xl_reply[x_name].value.string,
                                     (uint8) xl_reply[x_name].length,
                                     xl_reply[x_size].value.real,
                                     FALSE);           /* Bitmap-ness or otherwise not returned by PCL_MISCOP_XL */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("FontName = \"%s\", SymbolSet = %d, CharSize = %f CharBoldness = %f (point size = %f) -> Postscript Font \"%s\" PointSize = %f (which may or may not be a valid Postscript font)",
                 pclxl_debug_font_name(pclxl_font_name, pclxl_font_name_len, pclxl_font_name_type),
                 symbol_set,
                 char_size,
                 graphics_state->char_details.char_boldness,
                 point_size,
                 graphics_state->char_details.current_font.ps_font_name,
                 graphics_state->char_details.current_font.ps_font_point_size));

    return TRUE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("Failed to find Postscript font equivalent to PCLXL font called \"%s\"",
               pclxl_debug_font_name(pclxl_font_name, pclxl_font_name_len, pclxl_font_name_type)));

  if ( (config_params->default_font_name_len > 0) &&
       (pclxl_set_default_font(pclxl_context,
                               config_params->default_font_name,
                               config_params->default_font_name_len,
                               point_size,
                               symbol_set,
                               outline_char_path)) )
  {
    /* pfin_miscop() succeeded but did not return a result
     * This means that we failed to find
     * (an approximation of) the requested font
     *
     * So we tried to find an alternative substitute font
     * and this succeeded, so we must log a font substitution
     * warning
     */
     /** \todo we need to supply both font names to the warning handler
     */

    (void) pclxl_font_substitution_warning(__FILE__, __LINE__,
                                           pclxl_context,
                                           PCLXL_SS_TEXT,
                                           pclxl_font_name,
                                           pclxl_font_name_len,
                                           pclxl_context->config_params.default_font_name,
                                           pclxl_context->config_params.default_font_name_len);

    return TRUE;
  }
  else
  {
    /*
     * We have failed to find
     * (an approximation of) the requested font
     * and we have also failed to substitute an alternate font
     */

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND,
                                    pclxl_font_name,
                                    pclxl_font_name_len,
                                    ("Failed to SetFont([PCLXL] FontName = \"%s\", char_size = %f (point_size = %f), SymbolSet = %d",
                                     (char*) pclxl_font_name,
                                     char_size,
                                     point_size,
                                     symbol_set));

    return FALSE;
  }

  /*NOTREACHED*/
}

/*
 * pcl5_scan_integer() scans a character array looking for
 * a sequence of (decimal) digit characters
 * (optionally starting with a "+" or "-" sign)
 *
 * It reads the digits (and optional leading sign character)
 * and converts them into an integer value.
 *
 * It assumes that it has been invoked at the start of the integer digit sequence
 * I.e. the integer starts from string index 0 (zero)
 *
 * It returns the number of characters read/"consumed"
 * and the character *after* the integer digit character sequence
 * (which it also includes within the "consumed" count)
 */

static Bool
pcl5_scan_integer(uint8*  pcl5_select_font_data,
                  uint32  pcl5_select_font_data_len,
                  int32*  p_integer,
                  uint8*  trailing_char,
                  uint32* char_advance)
{
  uint32 i = 0;
  uint8 c = 0;
  uint32 integer = 0;
  int8 sign = 1;

  /*
   * While there is some data left
   * and this data starts with a (decimal) digit
   * We accumulate the integer value represented by these digits
   */

  while ( (i < pcl5_select_font_data_len) &&
          ((isdigit(c = pcl5_select_font_data[i])) ||
           ((i == 0) && (c == '-')) ||
           ((i == 0) && (c == '+'))
          )
        )
  {
    if (c == '+')
    {
      sign = 1;
    }
    if (c == '-')
    {
      sign = -1;
    }
    else
    {
      integer = ((integer * 10) + (sign * (c - '0')));
    }

    i++;
  }

  if ( i == 0 )
  {
    /*
     * There were no digits found
     * and no characters "consumed*
     */

    *char_advance = 0;

    return FALSE;
  }
  else if ( i < pcl5_select_font_data_len )
  {
    /*
     * At least one character was consumed
     * We have presumably accumulated an integer value
     * and there is at least one character remaining
     * So we return 3 things:
     *
     * 1) The acumulated integer
     * 2) The character immediately after it
     * 3) The total number of characters consumed
     *    (which includes the trailing non-digit char)
     */

    *p_integer = integer;
    *trailing_char = pcl5_select_font_data[i++];
    *char_advance = i;

    return TRUE;
  }
  else
  {
     /*
      * We have "consumed" at least one character
      * and this/these character(s) were all digits
      * but in consuming them we hit the end of the string
      * *without* encountering a trailing non-digit
      */

     *p_integer = integer;
     *trailing_char = '\0';
     *char_advance = i;

     return TRUE;
  }

  /*NOTREACHED*/
}

/*
 * pcl5_scan_decimal_places() scans a character array
 * looking for (decimal) digit characters which it assumes
 * to be the digits *after* the decimal point for a real/floating point number
 *
 * It is passed a pointer to a float which it assumes contains
 * the whole number part of the real value
 * and from which it derives the sign of the value
 *
 * It is intended that pcl5_scan_decimal_places() is called whenever
 * pcl5_scan_integer() returns a '.' (dot) as the trailing character
 *
 * Like pcl5_scan_integer(), pcl5_scan_decimal_places() also returns
 * the trailing character and the total "consumed" character count
 */

static Bool
pcl5_scan_decimal_places(uint8*  pcl5_select_font_data,
                         uint32  pcl5_select_font_data_len,
                         float*  p_real,
                         uint8*  trailing_char,
                         uint32* char_advance)
{
  uint32 i = 0;
  uint8 c = 0;
  float real = *p_real;
  float divisor = (float) (real < 0.0 ? -10.0 : 10.0);

  /*
   * While there is some data left
   * and this data starts with a (decimal) digit
   * We accumulate the fractional value represented by these digits
   * divided by a "divisor" (that is multiplied by 10
   * as each successive decimal place digit is consumed)
   */

  while ( (i < pcl5_select_font_data_len) &&
          (isdigit(c = pcl5_select_font_data[i]))
          /*
           * Note that we explicitly do *not* allow leading '+' or '-' characters
           * because we are expecting these to have been handled while
           * the whole-number part of this real was read
           */
        )
  {
    real = (real + ((c - '0') / divisor));

    divisor *= 10;

    i++;
  }

  if ( i == 0 )
  {
    /* There were no digits found
     * and no characters "consumed*
     */
    /** \todo Is this actually an error?
     * Are we allowed to accept "<digits>." as a valid real value?
     */

    *char_advance = 0;

    return FALSE;
  }
  else if ( i < pcl5_select_font_data_len )
  {
    /*
     * At least one character was consumed
     * We have presumably accumulated an integer value
     * and there is at least one character remaining
     * So we return 3 things:
     *
     * 1) The acumulated integer
     * 2) The character immediately after it
     * 3) The total number of characters consumed
     *    (which includes the trailing non-digit char
     */

    *p_real = real;

    *trailing_char = pcl5_select_font_data[i++];

    *char_advance = i;

    return TRUE;
  }
  else
  {
     /*
      * We have "consumed" at least one character
      * and this/these character(s) were all digits
      * but in consuming them we hit the end of the string
      * *without* encountering a trailing non-digit
      */

     *p_real = real;

     *trailing_char = '\0';

     *char_advance = i;

     return TRUE;
  }

  /*NOTREACHED*/
}

/*
 * pcl5_scan_font_select_data() is passed the (remainder of)
 * a PCLFontSelect attribute string.
 *
 * It extracts the next available font selection attribute
 * which basically consists of an integer or real value
 * and an associated character "tag" that identifies the attribute
 *
 * If it finds an attribute, it returns the integer or real value,
 * the associated "tag" and the total number of bytes of data
 * "consumed" as part of extracting this tag
 * (This "consumed-byte-count" is effectively the number of characters
 * that the caller needs to advance through the PCLSelectFont data
 * to request the next attribute)
 *
 * If it fails to find an integer-or-real value *AND* a tag
 * then it returns FALSE (and will probably have logged an error saying why)
 * but it will return a char_advance of zero in this case
 *
 * If it finds no more parameters, then it returns FALSE
 * but will return any neccessary char_advance so that
 * the caller can detect that the total char_advances result
 * in reaching the end of the PCLSelectFont data
 */

static Bool
pcl5_scan_font_select_data(PCLXL_PARSER_CONTEXT parser_context,
                           uint8*  pcl5_select_font_data,
                           uint32  pcl5_select_font_data_len,
                           int32*  p_integer,
                           float*  p_real,
                           uint8*  pcl5_select_font_tag,
                           uint32* char_advance)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  uint32 i = 0;
  uint8  c = 0;

  /*
   * Advance over any leading 'Esc' character(s)
   * (or any whitespace)
   */

  while ( (i < pcl5_select_font_data_len) &&
          (((c = pcl5_select_font_data[i]) == 0x1b) ||
           (isspace(c))) ) i++;

  /*
   * Ok "i" now points at either the first non-Esc non-whitespace character
   * or it points beyond the end of the data
   */

  if ( i == pcl5_select_font_data_len )
  {
    /**
     * We have exhausted the string
     * which means that it consisted entirely
     * of "Esc" characters and/or whitespace
     *
     * Do we need to treat this as any sort of error?
     * Or is this the valid termination condition?
     * For the moment we will return FALSE to the caller,
     * but do not log an error
     */

    *char_advance = i;

    return FALSE;
  }
  else if ( (i < (pcl5_select_font_data_len - 1)) &&
            (pcl5_select_font_data[i] == '(') &&
            (isdigit(pcl5_select_font_data[i + 1])) )
  {
    /* This PCL5 "command" represents the SymbolSet
     * which consists of an integer followed by a terminating character
     *
     * The SymbolSet (integer value) is the
     * (<integer_part> * 32) + (<uppercase_char_ASCII_code> - 64)
     * Or
     * (<integer_part> * 32) + (<lowercase_char_ASCII_code> - 96)
     */
    /** \todo we may which to detect a number followed by a '#'
     * which then represents a FontID rather than a SymbolSet
     */

    uint32 j = 0;
    int32 symbol_set_1 = 0;
    uint8 symbol_set_2 = 0;

    if ( pcl5_scan_integer(&pcl5_select_font_data[(i + 1)],
                           (pcl5_select_font_data_len - (i + 1)),
                           &symbol_set_1,
                           &symbol_set_2,
                           &j) )
    {
      *p_integer = ((symbol_set_1 * 32) + (symbol_set_2 & 31));

      *p_real = 0.0;

      *char_advance = (i + 1 + j);

      *pcl5_select_font_tag = c;

      return TRUE;
    }
    else
    {
      *char_advance = 0; /* *char_advance = (i + j); */

      return FALSE;
    }
  }
  else if ( (i < (pcl5_select_font_data_len - 1)) &&
            (pcl5_select_font_data[i] == '(') &&
            (pcl5_select_font_data[i + 1] == 's') )
  {
    /*
     * This PCL5 "command" represents the remaining font attributes
     * like height, pitch, stroke_weight etc.
     * In this case we ignore the command code itself
     * and instead read the first in a sequence of numeric values
     *
     * Note that we attempt to read an integer value
     * but if it turns out to end in a "." (decimal point)
     * then we make a further attempt to read a real/floating point value
     *
     * The obvious way to do this is to recursively call *this* function
     * which will then fall into the next "else"-branch
     * which looks for an integer or real followed by a single letter
     */

    Bool recursive_call_result =
      pcl5_scan_font_select_data(parser_context,
                                 &pcl5_select_font_data[(i + 2)],
                                 (pcl5_select_font_data_len - (i + 2)),
                                 p_integer,
                                 p_real,
                                 pcl5_select_font_tag,
                                 char_advance);

    if ( recursive_call_result )
    {
      *char_advance += (i + 2);

      return TRUE;
    }
    else
    {
      *char_advance = 0;

      return FALSE;
    }
  }
  else if ( (isdigit(c)) || (c == '+') || (c == '-') )
  {
    /*
     * This is the first digit (or sign-character)
     * in what is either an integer or real value
     * that is followed by a single letter identifier
     *
     * We therefore attempt to read an integer value
     * and then test the following/trailing non-digit character.
     *
     * If it is a '.' i.e. decimal point, then we read the data beyond
     * the decimal point expecting them to be decimal place digits
     */

    uint32 j = 0;
    uint32 k = 0;
    uint8 dp = 0;
    int32 integer = 0;
    float real = 0.0;

    if ( ! pcl5_scan_integer(&pcl5_select_font_data[i],
                             (pcl5_select_font_data_len - i),
                             &integer,
                             &dp,
                             &j) )
    {
      /*
       * We have failed to read an integer character
       *
       * Which is pretty impressive given that we *know* it begins
       * with at least one digit character
       *
       * We must log an error and return false
       */

      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_TEXT,
                                 PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                                 ("Failed to read (an integer from) PCLSelectFont font attribute from \"%s\"",
                                  &pcl5_select_font_data[i]));

      *char_advance = 0; /* *char_advance = (i + j); */

      return FALSE;
    }
    else if ( dp != '.' )
    {
      /*
       * We have successfully read an integer
       * and it was *not* followed by a '.' (i.e. a decimal place)
       *
       * So we can return this to the caller
       * as the next (integer) attribute
       */

      *pcl5_select_font_tag = dp;

      *p_integer = integer;

      *p_real = (float) integer;

      *char_advance = (i + j);

      return TRUE;
    }
    else if ( !(real = (float) integer,
                pcl5_scan_decimal_places(&pcl5_select_font_data[(i + j)],
                                         (pcl5_select_font_data_len - (i + j)),
                                         &real,
                                         &dp,
                                         &k)) )
    {
      /*
       * We successfully read the integral part of this font selection attribute
       * and it ended with a decimal point
       *
       * But we have then failed to read the subsequent decimal places
       */

      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_TEXT,
                                 PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                                 ("Failed to read (a real from) PCLSelectFont font attribute from \"%s\"",
                                  &pcl5_select_font_data[i]));

      *char_advance = 0; /* *char_advance = (i + j + k); */

      return FALSE;
    }
    else
    {
      /*
       * We have successfully read a real value
       */

      *p_real = real;

      *pcl5_select_font_tag = dp;

      *char_advance = (i + j + k);

      return TRUE;
    }
  }
  else
  {
    /*
     * Oh dear, the font selection data has not been recognized
     * as a valid collection of attributes
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                               ("Failed to read next PCLSelectFont font attribute from \"%s\"",
                                &pcl5_select_font_data[i]));

    *char_advance = 0; /* *char_advance = i; */

    return FALSE;
  }
}

/*
 * pclxl_get_default_pcl5_font() basically attempts to select a PCL5 font
 * using either some hard-coded font selection criteria
 * or (eventually) using some configurable font ID/location
 * to establish the default PCL5 font selection criteria.
 *
 * These initial criteria, cumulatively supplemented by
 * any/all criteria explicitly specified in any PCLSelectFont attributes
 * are thereafter used to select PCL5 fonts.
 *
 * pclxl_set_pcl5_font() already picks up any/all PCL5 font selection
 * criteria from within a substructure within a PCLXL_FONT_DETAILS
 * So all we do here is to initialize this sub-structure
 *
 * Note that a significant part of the following function's content
 * has effectively been *copied* from PCL5's "fontselection.c"
 * specifically default_font_sel_info() and do_pfin_select_by_criteria()
 * because we could not directly *share* these functions
 * due to dependence upon several PCL5-specific typedefs/structures
 */

static Bool
pclxl_get_default_pcl5_font(PCLXL_CONTEXT pclxl_context,
                            PCLXL_FONT_DETAILS font_details)
{
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCL5_FONT_SELECTION_CRITERIA* pcl5_font_selection_criteria =
    &font_details->pcl5_font_selection_criteria;

#define PC_8 341 /* (10*32) + (85-64) */
#define COURIER 4099

  static sw_datum pcl_miscop_specify_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_specify_params + 1, 10),

    SW_DATUM_INTEGER(PCL_MISCOP_SPECIFY), /* reason code (0 == select by criteria) */
    SW_DATUM_INTEGER(PC_8),               /* symbolset */
    SW_DATUM_INTEGER(0),                  /* spacing */
    SW_DATUM_FLOAT(SW_DATUM_0_0F),        /* pitch (cpi) */
    SW_DATUM_FLOAT(SW_DATUM_0_0F),        /* height (pts) */
    SW_DATUM_INTEGER(0),                  /* style */
    SW_DATUM_INTEGER(0),                  /* weight */
    SW_DATUM_INTEGER(COURIER),            /* typeface */
    SW_DATUM_BOOLEAN(FALSE),              /* exclude bitmap set to FALSE */
    SW_DATUM_INTEGER(0),                  /* print resolution */
    SW_DATUM_INTEGER(0)                   /* char_boldness */
  };

  sw_datum* params = pcl_miscop_specify_params;
  sw_datum* reply = pcl_miscop_specify_params;

  sw_pfin_result pfin_miscop_result;

  if ( pcl5_font_selection_criteria->initialized )
  {
    /*
     * This set of PCL5 font selection criteria
     * has *already* / *previously* been initialized
     * (probably by a previous call to this exact function)
     *
     * The question is: Do we really want to *re-initialize*
     * this structure here?
     */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Already found the default PCL5 font. Why are we trying to *re-initialze* it?"));

    return TRUE;
  }

  params[4].value.real = 10.0;
  params[5].value.real = 12.0;

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_SPECIFY) to select the default PCL5 font"));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have failed to call pfin_miscop()
     * for some more disasterous reason than simply failing to find
     * the PCL5 font that matched our hard-coded criteria
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to look up default PCL5 font"));

    return FALSE;
  }
  else if ( reply != NULL )
  {
    HQASSERT((reply->type == SW_DATUM_TYPE_ARRAY && reply->length == 9),
             "Unexpected reply from pfin_miscop(PCL_MISCOP_SPECIFY)");
    HQASSERT((reply->owner == 0), "Unexpected reply owner");

    reply = (sw_datum*) reply->value.opaque;

    HQASSERT((reply[0].type == SW_DATUM_TYPE_STRING &&   /* PS font name (uint8 array) */
              reply[1].type == SW_DATUM_TYPE_FLOAT &&    /* PS font size (pts) */
              reply[2].type == SW_DATUM_TYPE_FLOAT &&    /* HMI (ems) - Ignored by PCLXL */
              reply[3].type == SW_DATUM_TYPE_INTEGER &&  /* symbolset type - Ignored by PCLXL */
              reply[4].type == SW_DATUM_TYPE_INTEGER &&  /* spacing (ems) - Ignored by PCLXL */
              reply[5].type == SW_DATUM_TYPE_FLOAT &&    /* underline offset (ems) - Ignored by PCLXL */
              reply[6].type == SW_DATUM_TYPE_FLOAT &&    /* undeline thickness (ems) - Ignored by PCLXL */
              reply[7].type == SW_DATUM_TYPE_INTEGER &&  /* FontID */
              reply[8].type == SW_DATUM_TYPE_BOOLEAN),   /* is bitmapped */
             "Unexpected reply array from pfin_miscop(PCL_MISCOP_SPECIFY)") ;

    /*
     * We must now transcribe the various "font selection criteria"
     * from various parts of *both* the params and the reply
     * into the pcl5_font_selection_criteria
     */

    pcl5_font_selection_criteria->spacing    = reply[4].value.integer;
    pcl5_font_selection_criteria->height     = reply[1].value.real;
  }

  pcl5_font_selection_criteria->symbol_set = params[2].value.integer;
  pcl5_font_selection_criteria->pitch      = params[4].value.real;
  pcl5_font_selection_criteria->style      = params[6].value.integer;
  pcl5_font_selection_criteria->weight     = params[7].value.integer;
  pcl5_font_selection_criteria->typeface   = params[8].value.integer;

  pcl5_font_selection_criteria->initialized++;

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("Default PCL5 font selection criteria: symbol_set = %d, spacing = %d, pitch = %f, height = %f, style = %d, weight = %d, typeface = %d",
               pcl5_font_selection_criteria->symbol_set,
               pcl5_font_selection_criteria->spacing,
               pcl5_font_selection_criteria->pitch,
               pcl5_font_selection_criteria->height,
               pcl5_font_selection_criteria->style,
               pcl5_font_selection_criteria->weight,
               pcl5_font_selection_criteria->typeface));

  return TRUE;
}

static Bool
pclxl_set_pcl5_font(PCLXL_PARSER_CONTEXT parser_context,
                    uint8*               pcl5_select_font_data,
                    uint32               pcl5_select_font_data_len,
                    Bool                 outline_char_path)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_NON_GS_STATE non_gs_state = &pclxl_context->non_gs_state;
  PCLXL_CHAR_DETAILS char_details = &graphics_state->char_details;
  PCLXL_FONT_DETAILS font_details = &char_details->current_font;
  PCL5_FONT_SELECTION_CRITERIA* pcl5_font_selection_criteria = &font_details->pcl5_font_selection_criteria;

  static sw_datum pcl_miscop_specify_params[] =
  {
    SW_DATUM_ARRAY(pcl_miscop_specify_params + 1, 10),

    SW_DATUM_INTEGER(PCL_MISCOP_SPECIFY), /* reason code (0 == select by criteria) */
    SW_DATUM_INTEGER(0),                  /* symbolset */
    SW_DATUM_INTEGER(0),                  /* spacing */
    SW_DATUM_FLOAT(SW_DATUM_0_0F),        /* pitch (cpi) */
    SW_DATUM_FLOAT(SW_DATUM_0_0F),        /* height (pts) */
    SW_DATUM_INTEGER(0),                  /* style */
    SW_DATUM_INTEGER(0),                  /* weight */
    SW_DATUM_INTEGER(0),                  /* typeface */
    SW_DATUM_BOOLEAN(FALSE),              /* exclude bitmap set to FALSE */
    SW_DATUM_INTEGER(0)                   /* char_boldness */
  };

  enum { /* Parameter indices (into the above array) */
    p_array = 0, p_reason, p_ss, p_space, p_pitch, p_height, p_style, p_weight,
    p_font, p_exclude, p_bold
  } ;

  enum { /* reply indices */
    r_name, r_size, r_hmi, r_sstype, r_space, r_offset, r_thick,
    r_ref, r_bitmap
  } ;

  sw_datum* params = pcl_miscop_specify_params;
  sw_datum* reply = pcl_miscop_specify_params;

  sw_pfin_result pfin_miscop_result;

  /*
   * We also need to supply the "char (faux) boldness value"
   * which is the PCLXL char boldness in the range 0.0 to 1.0
   * multiplied by 32768 and then rounded to the nearest integer
   *
   * However the PCL_MISCOP_XL parameter list does not yet support this
   */

  int char_boldness = (outline_char_path ?
                       0 :
                       (int) ((char_details->char_boldness * 32768) + 0.5));

  /*
   * We must now scan the pcl5_select_font_data()
   * to pull out the various font selection attributes
   * and complete the above structure accordingly
   *
   * Note that param[1] and param[9] are always untouched
   * because they are already statically initialized
   * to the correct values above.
   */

  uint32 i = 0;
  uint32 j = 0;
  uint8 c = 0;
  int32 integer = 0;
  float real = 0.0;

  /*
   * Note that as we are about to "parse" a PCL5SelectFont "string"
   * that may not necessarily contaname in all the possible font specifiers
   * we need to make sure that we re-initialize the pcl_miscop_specify_params
   * back to sensible defaults each time this function is called
   *
   * Note that a value of 0 for typeface is *not* an acceptable
   * default as zero is used as the typeface for user-defined
   * a.k.a. "soft" fonts
   */

  if ( !pcl5_font_selection_criteria->initialized )
    (void) pclxl_get_default_pcl5_font(pclxl_context,
                                         font_details);

  params[p_ss    ].value.integer = pcl5_font_selection_criteria->symbol_set;
  params[p_space ].value.integer = pcl5_font_selection_criteria->spacing;
  params[p_pitch ].value.real    = pcl5_font_selection_criteria->pitch,
  params[p_height].value.real    = pcl5_font_selection_criteria->height,
  params[p_style ].value.integer = pcl5_font_selection_criteria->style,
  params[p_weight].value.integer = pcl5_font_selection_criteria->weight,
  params[p_font  ].value.integer = pcl5_font_selection_criteria->typeface,
  params[p_bold  ].value.integer = char_boldness;

  while ( (i < pcl5_select_font_data_len) &&
          (pcl5_scan_font_select_data(parser_context,
                                      &pcl5_select_font_data[i],
                                      (pcl5_select_font_data_len - i),
                                      &integer,
                                      &real,
                                      &c,
                                      &j)) )
  {
    switch ( c )
    {
    case '(':

      /* SymbolSet */

      params[p_ss].value.integer = (int) integer;

      break;

    case 'P':
    case 'p':

      /* Spacing */

      params[p_space].value.integer = (int) integer;

      break;

    case 'H':
    case 'h':

      /* Pitch */

      params[p_pitch].value.real = real;

      break;

    case 'V':
    case 'v':

      /* Height */

      params[p_height].value.real = real;

      break;

    case 'S':
    case 's':

      /* Style */

      params[p_style].value.integer = (int) integer;

      break;

    case 'B':
    case 'b':

      /* StrokeWeight */

      params[p_weight].value.integer = (int) integer;

      break;

    case 'T':
    case 't':

      /* Typeface */

      params[p_font].value.integer = (int) integer;

      break;

    default:

      /*
       * Some other font selection attribute was encountered?!
       * We don't know what to do with it.
       * Is this an error (or a warning)?
       * Or do we simply ignore it?
       */

      (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                      PCLXL_SS_TEXT,
                                      PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND,
                                      pcl5_select_font_data,
                                      pcl5_select_font_data_len,
                                      ("Unexpected PCLSelectFont attribute '%s' found while processing \"%s\"",
                                       c,
                                       pcl5_select_font_data));

      return FALSE;

      break;
    }

    integer = 0;
    real = 0.0;
    i += j;
    j = 0;
    c = 0;
  }

  if ( (i + j) < pcl5_select_font_data_len )
  {
    /*
     * We failed to process the entire PCLSelectFont data
     * Again I can only assume that this is an error to be reported
     * and acted upon
     */

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND,
                                    pcl5_select_font_data,
                                    pcl5_select_font_data_len,
                                    ("Failed to process all of the PCLSelectFont data \"%s\", Remainder = \"%s\"",
                                     pcl5_select_font_data,
                                     &pcl5_select_font_data[i]));

    return FALSE;
  }

  /*
   * Ok, we have populated as much (or as little?) of the
   * params as is necessary to actually
   * attempt a font selection (using the "ufst" PFIN module)
   */

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("About to call pfin_miscop(PCL_MISCOP_SPECIFY) to select a PCL5 font by font PCL[5]SelectFont criteria"));

  if ( (pfin_miscop_result = pfin_miscop(non_gs_state->ufst,
                                         &reply)) != SW_PFIN_SUCCESS )
  {
    /*
     * We have received an error from pfin_miscop()
     * This is more serious a problem
     * than merely failing to find
     * (a nearest approximation of) the requested font
     *
     * So this is a non-recoverable failure
     */

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    PCLXL_INTERNAL_ERROR,
                                    pcl5_select_font_data,
                                    pcl5_select_font_data_len,
                                    ("Failed to look up PCLSelectFont = \"%s\" in UFST module",
                                     pcl5_select_font_data));

    return FALSE;
  }
  else if ( reply != NULL )
  {
    HQASSERT((reply->type == SW_DATUM_TYPE_ARRAY && reply->length == 9),
             "Unexpected reply from pfin_miscop(PCL_MISCOP_SPECIFY)");
    HQASSERT((reply->owner == 0), "Unexpected reply owner");

    reply = (sw_datum*) reply->value.opaque;

    HQASSERT((reply[r_name  ].type == SW_DATUM_TYPE_STRING &&   /* PS font name (uint8 array) */
              reply[r_size  ].type == SW_DATUM_TYPE_FLOAT &&    /* PS font size (pts) */
              reply[r_hmi   ].type == SW_DATUM_TYPE_FLOAT &&    /* HMI (ems) - Ignored by PCLXL */
              reply[r_sstype].type == SW_DATUM_TYPE_INTEGER &&  /* symbolset type - Ignored by PCLXL */
              reply[r_space ].type == SW_DATUM_TYPE_INTEGER &&  /* spacing (ems) - Ignored by PCLXL */
              reply[r_offset].type == SW_DATUM_TYPE_FLOAT &&    /* underline offset (ems) - Ignored by PCLXL */
              reply[r_thick ].type == SW_DATUM_TYPE_FLOAT &&    /* undeline thickness (ems) - Ignored by PCLXL */
              reply[r_ref   ].type == SW_DATUM_TYPE_INTEGER &&  /* FontID */
              reply[r_bitmap].type == SW_DATUM_TYPE_BOOLEAN),   /* is bitmapped */
             "Unexpected reply array from pfin_miscop(PCL_MISCOP_SPECIFY)") ;

    (void) pcl5_record_font_details(graphics_state,
                                    PCLXL_FS_PFIN_FONT_SELECTED,
                                    PCLXL_FT_PCL5_SELECT_FONT,
                                    pcl5_select_font_data,           /* pcl5 select font string */
                            (uint8) pcl5_select_font_data_len,       /* pcl5 select font length */
                                    0.0,                             /* PCLXL char size */
                                    reply[r_ref    ].value.integer,  /* soft font ID */
                                    params[p_ss    ].value.integer,  /* symbol set */
                                    reply[r_space  ].value.integer,  /* spacing */
                                    params[p_pitch ].value.real,     /* pitch */
                                    reply[r_size   ].value.real,     /* height */
                                    params[p_style ].value.integer,  /* style */
                                    params[p_weight].value.integer,  /* weight */
                                    params[p_font  ].value.integer,  /* typeface */
                           (uint8*) reply[r_name   ].value.string,   /* Postscript font name */
                            (uint8) reply[r_name   ].length,         /* Postscript font name length */
                                    reply[r_size   ].value.real,     /* font point size */
                                    reply[r_bitmap ].value.boolean); /* this font is a bitmap font */

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("PCLSelectFont = \"%s\" + CharBoldness = %f -> Postscript Font \"%s\" PointSize = %f (Which may or may not be a valid Postscript font)",
                 pcl5_select_font_data,
                 char_details->char_boldness,
                 graphics_state->char_details.current_font.ps_font_name,
                 graphics_state->char_details.current_font.ps_font_point_size));

    return TRUE;
  }

  PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
              ("Failed to find Postscript font equivalent to PCL[5]SelectFont data"));

  if ( (config_params->default_font_name_len > 0) &&
       (pclxl_set_default_font(pclxl_context,
                               config_params->default_font_name,
                               config_params->default_font_name_len,
                               params[p_height].value.real,
                               params[p_ss    ].value.integer,
                               outline_char_path)) )
  {
    /*
     * pfin_miscop() succeeded but did not return a result
     * This means that we failed to find
     * (an approximation of) the requested font
     *
     * So we tried to find an alternative substitute font
     * and this succeeded, so we must log a font substitution
     * warning
     */

    (void) pclxl_font_substitution_warning(__FILE__, __LINE__,
                                           pclxl_context,
                                           PCLXL_SS_TEXT,
                                           pcl5_select_font_data,
                                           pcl5_select_font_data_len,
                                           pclxl_context->config_params.default_font_name,
                                           pclxl_context->config_params.default_font_name_len);

    return TRUE;
  }
  else
  {
    /*
     * We have failed to find
     * an approximation of) the requested font
     * and we have also failed to substitute an alternate font
     */

    (void) PCLXL_FONT_ERROR_HANDLER(pclxl_context,
                                    PCLXL_SS_TEXT,
                                    PCLXL_FONT_UNDEFINED_NO_SUBSTITUTE_FOUND,
                                    pcl5_select_font_data,
                                    pcl5_select_font_data_len,
                                    ("Failed to SetFont(PCLSelectFont = \"%s\")",
                                     pcl5_select_font_data));

    return FALSE;
  }

  /*NOTREACHED*/
}

Bool
pclxl_set_courier_weight(PCLXL_CONTEXT pclxl_context,
                         uint32        courier_weight)
{
  uint8 ps_string[64];

  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " <</UFST <</DarkCourier %s >> >> setpfinparams ",
                  (courier_weight ? "true" : "false"));

  if (!pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                sizeof(ps_string)))
      return FALSE;

  /* \todo : Applying configuration for all flavors of PFIN module is OK,
   * but a bit wasteful.
   * Will need to implement proper switch of PFIN modules.
   */
  (void) swncopyf(ps_string,
                  sizeof(ps_string),
                  (uint8*) " <</FF <</DarkCourier %s >> >> setpfinparams ",
                  (courier_weight ? "true" : "false"));

  return pclxl_ps_run_ps_string(pclxl_context,
                                ps_string,
                                sizeof(ps_string));
}

Bool
pclxl_set_default_font(PCLXL_CONTEXT        pclxl_context,
                       uint8*               font_name,
                       uint32               font_name_len,
                       PCLXL_SysVal         point_size,
                       uint32               symbol_set,
                       Bool                 outline_char_path)
{
  static uint8 recursion_depth = 0;

  PCLXL_PARSER_CONTEXT parser_context = pclxl_context->parser_context;
  PCLXL_NON_GS_STATE   non_gs_state   = &pclxl_context->non_gs_state;

  if ( recursion_depth++ > 0 )
  {
    /*
     * We have a problem here: pclxl_set_default_font()
     * has been called recursively
     *
     * This is almost certainly because pclxl_set_pclxl_font()
     * has failed to set the default font, typically because
     * either it has been mis-named (or because it is not exactly 16 characters long)
     */

    (void) PCLXL_ERROR_HANDLER(pclxl_context,
                               PCLXL_SS_TEXT,
                               PCLXL_INTERNAL_ERROR,
                               ("Failed to set default font \"%s\", point_size = %f, symbol_set = %d. (Hint: is this a valid font name and is the name exactly 16 characters long?",
                                font_name,
                                point_size,
                                symbol_set));

    recursion_depth--;

    return FALSE;
  }
  else if ( (font_name_len > 0) &&
            (font_name[0] == 0x1b))
  {
    /*
     * There is a default font name but it starts with an ESC (0x1b) character
     * This means that we interpret it as being a PCL5 select font by criteria
     */

    Bool set_pcl5_font_result;

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Setting *default* PCL5 font \"%s\"",
                 font_name));

    set_pcl5_font_result = pclxl_set_pcl5_font(parser_context,
                                               (uint8*) font_name,
                                               (uint32) font_name_len,
                                               outline_char_path);

    recursion_depth--;

    return set_pcl5_font_result;
  }
  else if ( font_name_len > 0 )
  {
    Bool set_pclxl_font_result;

    PCLXL_SysVal font_scale = (point_size *
                               non_gs_state->units_per_measure.res_y /
                               pclxl_ps_units_per_pclxl_uom(non_gs_state->measurement_unit));

    PCLXL_DEBUG(PCLXL_DEBUG_FONTS,
                ("Setting *default* PCLXL font \"%s\", point_size = %f/font_scale = %f, symbol_set = %d",
                 pclxl_debug_font_name(font_name, (uint32) font_name_len, PCLXL_DT_UByte_Array),
                 point_size,
                 font_scale,
                 symbol_set));

    set_pclxl_font_result = pclxl_set_pclxl_font(parser_context,
                                                 (uint8*) font_name,
                                                 (uint32) font_name_len,
                                                 (((font_name_len > 16) &&
                                                   ((font_name_len % 2) == 0)) ?
                                                  PCLXL_DT_UInt16_Array :
                                                  PCLXL_DT_UByte_Array),
                                                 font_scale,
                                                 symbol_set,
                                                 outline_char_path);

    recursion_depth--;

    return set_pclxl_font_result;
  }
  else
  {
    (void) PCLXL_WARNING_HANDLER(pclxl_context,
                                 PCLXL_SS_TEXT,
                                 PCLXL_NO_CURRENT_FONT,
                                 ("There is no *default* font, but we have tried to select the default"));

    recursion_depth--;

    return FALSE;
  }
}

/*
 * pclxl_set_font() is passed a PCLXL graphics state
 * and a PCLXL_FONT_DETAILS that specifies a font that
 * needs to be installed as the current font
 * (indeed it may even already be the current font)
 * and whether or not we are drawing filled or outline character (paths)
 *
 * pclxl_set_font() examines the font's pclxl_font_state
 * to decide what (if anything) needs doing to make this font
 * the current (Postscript) font
 *
 * If the font't state is PCLXL_FS_NO_CURRENT_FONT
 * then it calls pclxl_set_current_font()
 * then calls pclxl_ps_select_font()
 *
 * If the font's state is PCLXL_FS_PCLXL_FONT_SET
 * then it call either pclxl_set_pclxl_font() or pclxl_set_pcl5_font()
 * (which may themselves call pclxl_set_default_font())
 * and then calls pclxl_ps_select_font()
 *
 * If the font's state is already PCLXL_FS_PFIN_FONT_SELECTED
 * then it simply calls pclxl_ps_select_font()
 */

Bool
pclxl_set_font(PCLXL_PARSER_CONTEXT parser_context,
               PCLXL_FONT_DETAILS   font_details,
               Bool                 outline_char_path)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_CHAR_DETAILS char_details = &pclxl_context->graphics_state->char_details;

  HQASSERT(font_details->pclxl_font_state <= PCLXL_FS_POSTSCRIPT_FONT_SELECTED, "Invalid font state");
  HQASSERT(font_details->pclxl_font_type <= PCLXL_FT_PCL5_SELECT_FONT, "Invalid font type");

  if ( font_details->pclxl_font_state == PCLXL_FS_NO_CURRENT_FONT )
  {
    /*
     * Oh dear, there is no current PCLXL font.
     * According to the PCLXL Protocol Class Specification
     * This means that we must report NoCurrentFont
     *
     * However, it appears that HP printers often supply
     * a default font so that text is *always* output in some shape or form
     *
     * We model this by optionally having a configuration-parameter supplied
     * default font name/point size/symbol set
     *
     * If these parameters are supplied, then we attempt to
     * select this default font.
     *
     * If this selection fails or they are not defined
     * then we log the NoCurrentFont error as per the spec
     */

    PCLXL_CONFIG_PARAMS config_params = &pclxl_context->config_params;

    if ( (config_params->strict_pclxl_protocol_class) ||
         (strlen((char*) config_params->default_font_name) == 0) )
    {
      (void) PCLXL_ERROR_HANDLER(pclxl_context,
                                 PCLXL_SS_KERNEL,
                                 PCLXL_NO_CURRENT_FONT,
                                 ("There is no current font"));

      return FALSE;
    }
    else if ( !pclxl_set_default_font(pclxl_context,
                                      (font_details->pclxl_font_name_len ?
                                       font_details->pclxl_font_name :
                                       config_params->default_font_name),
                                      (font_details->pclxl_font_name_len ?
                                       font_details->pclxl_font_name_len :
                                       config_params->default_font_name_len),
                                      (font_details->ps_font_point_size ?
                                       font_details->ps_font_point_size :
                                       config_params->default_point_size),
                                      (font_details->symbol_set ?
                                       font_details->symbol_set:
                                       config_params->default_symbol_set),
                                      outline_char_path) )
    {
      return FALSE;
    }
  }

  if ( (font_details->pclxl_font_state == PCLXL_FS_PCLXL_FONT_SET) ||
       /*
        * Note that we always force a re-selection
        * whenever char_boldness is non-zero
        * because we need to switch between two different PFIN/UFST
        * fonts when switching between filled and outline character (paths)
        */
       (char_details->char_boldness) )
  {
    /*
     * The PCLXL font name or PCL5 font selection criteria have been set
     * But we appear not to have done a pfin_miscop() call to "select"
     * (i.e. retrieve the equivalent Postscript name) for this PCLXL/PCL5 font
     *
     * So we must go ahead and call one of:
     * pclxl_set_pclxl_font() or pclxl_set_pcl5_font()
     */

    if ( ((font_details->pclxl_font_type == PCLXL_FT_8_BIT_FONT_NAME) &&
          (!pclxl_set_pclxl_font(parser_context,
                                 font_details->pclxl_font_name,
                                 font_details->pclxl_font_name_len,
                                 PCLXL_DT_UByte_Array,
                                 font_details->pclxl_char_size,
                                 font_details->symbol_set,
                                 outline_char_path))) ||
         ((font_details->pclxl_font_type == PCLXL_FT_16_BIT_FONT_NAME) &&
          (!pclxl_set_pclxl_font(parser_context,
                                 font_details->pclxl_font_name,
                                 font_details->pclxl_font_name_len,
                                 PCLXL_DT_UInt16_Array,
                                 font_details->pclxl_char_size,
                                 font_details->symbol_set,
                                 outline_char_path))) ||
         ((font_details->pclxl_font_type == PCLXL_FT_PCL5_SELECT_FONT) &&
          (!pclxl_set_pcl5_font(parser_context,
                                font_details->pclxl_font_name,
                                font_details->pclxl_font_name_len,
                                outline_char_path))) )
    {
      return FALSE;
    }
  }

  if ( font_details->pclxl_font_state == PCLXL_FS_PFIN_FONT_SELECTED )
  {
    return pclxl_ps_select_font(pclxl_context, FALSE);
  }

  /*
   * If we get to here
   * The font state must already be at PCLXL_FS_POSTSCRIPT_FONT_SELECTED
   * So we simply return TRUE
   */

  return TRUE;
}

/*
 * Tag 0x6f SetFont
 */

Bool
pclxl_op_set_font(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[5] = {
#define SETFONT_FONT_NAME   (0)
    {PCLXL_AT_FontName},
#define SETFONT_CHAR_SIZE   (1)
    {PCLXL_AT_CharSize},
#define SETFONT_SYMBOL_SET  (2)
    {PCLXL_AT_SymbolSet},
#define SETFONT_PCL_SELECT_FONT (3)
    {PCLXL_AT_PCLSelectFont},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_ATTRIBUTE pcl_select_font_attr;
  uint8* font_name;
  PCLXL_SysVal char_size;
  uint32 font_name_len;
  uint32 symbol_set;
  PCLXL_TAG font_name_dt;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match_at_least_1(parser_context->attr_set, match,
                                        pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( pclxl_stream_min_protocol(pclxl_parser_current_stream(parser_context),
                                 PCLXL_PROTOCOL_VERSION_3_0) ) {
    /* PCLFontSelect */
    if ( match[SETFONT_PCL_SELECT_FONT].result ) {
      if ( match[SETFONT_FONT_NAME].result || match[SETFONT_CHAR_SIZE].result ||
           match[SETFONT_SYMBOL_SET].result ) {
        PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_COMBINATION,
                            ("Found normal font attributes with a PCL 5 font selection"));
        return(FALSE);
      }

      pcl_select_font_attr = match[SETFONT_PCL_SELECT_FONT].result;

      PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
                  ("SetFont(PCLSelectFont=\"%s\")",
                   pclxl_debug_font_name(pcl_select_font_attr->value.v_ubytes,
                                         pcl_select_font_attr->array_length,
                                         pcl_select_font_attr->data_type)));

      return(pclxl_set_pcl5_font(parser_context,
                                 pcl_select_font_attr->value.v_ubytes,
                                 pcl_select_font_attr->array_length,
                                 FALSE));
    }
  }

  /* No PCL 5 font selection - must have the normal attributes */
  if ( !(match[SETFONT_FONT_NAME].result && match[SETFONT_CHAR_SIZE].result &&
         match[SETFONT_SYMBOL_SET].result) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_MISSING_ATTRIBUTE,
                        ("XL font selection attribute missing with non PCL5 font selection"));
    return(FALSE);
  }

  /* FontName - uint16 font names allowed if not in strict mode */
  if ( (match[SETFONT_FONT_NAME].result->data_type == PCLXL_DT_UInt16_Array) &&
       pclxl_context->config_params.strict_pclxl_protocol_class ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_ILLEGAL_ATTRIBUTE_DATA_TYPE,
                        ("Illegal attribute data type - caught uint16 fontname when in strict mode."));
    return(FALSE);
  }
  font_name_dt = match[SETFONT_FONT_NAME].result->data_type;
  pclxl_attr_get_byte_len(match[SETFONT_FONT_NAME].result, &font_name, &font_name_len);
  /* CharSize */
  char_size = pclxl_attr_get_real(match[SETFONT_CHAR_SIZE].result);
  /* SymbolSet */
  symbol_set = pclxl_attr_get_uint(match[SETFONT_SYMBOL_SET].result);

  PCLXL_DEBUG((PCLXL_DEBUG_OPERATORS | PCLXL_DEBUG_FONTS),
              ("SetFont(FontName=\"%s\", CharSize = %f, SymbolSet = %d)",
               pclxl_debug_font_name(font_name, font_name_len, font_name_dt),
               char_size, symbol_set));

  return(pclxl_set_pclxl_font(parser_context, font_name, font_name_len,
                              font_name_dt, char_size, symbol_set, FALSE));
}

/*
 * Tag 0x7d SetCharBoldValue
 */

Bool
pclxl_op_set_char_bold_value(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARBOLDVAL_CHAR_BOLD_VALUE  (0)
    {PCLXL_AT_CharBoldValue | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_SysVal char_boldness;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharBoldValue */
  char_boldness = pclxl_attr_get_real(match[SETCHARBOLDVAL_CHAR_BOLD_VALUE].result);

  if ( (char_boldness < 0.0) || (char_boldness > 1.0) ) {
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_TEXT, PCLXL_ILLEGAL_ATTRIBUTE_VALUE,
                        ("CharBoldValue %f must be in the range 0.0 to 1.0", char_boldness));
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCharBoldValue(%f)", char_boldness));

  return(pclxl_set_char_boldness(pclxl_context->graphics_state, char_boldness));
}

/*
 * Tag 0x81 SetCharSubMode
 */

Bool
pclxl_op_set_char_sub_mode(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[2] = {
#define SETCHARSUBMODE_CHAR_SUB_MODE_ARRAY  (0)
    {PCLXL_AT_CharSubModeArray | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_GRAPHICS_STATE graphics_state = pclxl_context->graphics_state;
  PCLXL_ATTRIBUTE char_sub_mode_attr;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* CharSubModeArray */
  char_sub_mode_attr = match[SETCHARSUBMODE_CHAR_SUB_MODE_ARRAY].result;

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS,
              ("SetCharSubMode(CharSubModeArray [length = %d byte%s])",
               char_sub_mode_attr->array_length,
               (char_sub_mode_attr->array_length != 1 ? "s" : "")));

  return(pclxl_set_char_sub_modes(pclxl_context, &graphics_state->char_details,
                                  char_sub_mode_attr->value.v_ubytes,
                                  char_sub_mode_attr->array_length));
}

/******************************************************************************
* Log stripped */
