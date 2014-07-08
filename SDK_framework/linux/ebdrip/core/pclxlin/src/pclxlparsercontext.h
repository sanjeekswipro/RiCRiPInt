/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlparsercontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#ifndef __PCLXLPARSERCONTEXT_H__
#define __PCLXLPARSERCONTEXT_H__ 1

#include "mm.h"
#include "matrix.h"
#include "lists.h"

#include "pclxltypes.h"
#include "pclxlcontext.h"
#include "pclxlstream.h"
#include "pclxlimaget.h"

/**
 * During the parsing of a PCLXL data stream we maintain a "current
 * state" which is used to validate the order in which operator byte
 * codes are allowed to appear
 *
 * The transitions between the various states is governed using a
 * "state machine" that is defined in "states\operators.sm" from which
 * "pclxlsmtable.h" (and "pclxlsmtable.c") are automatically generated
 */

/**
 * \brief pclxl_scan_stream_object reads so-called "objects" from
 * the PCLXL data stream.
 *
 * These objects include the encoding (+ associated stream header),
 * data items (which consist of a type and value(s)),
 * attribute IDs operators and whitespace
 *
 * However pclxl_scan_stream_object() attempts to handle
 * most of these objects directly locally wherever possible.
 * So encodings, whitespace and data type+value(s) are not returned.
 *
 * Instead pclxl_scan_stream_object() only returns
 * "fully formed" (i.e. complete with data types, value(s) and ID) attributes
 * and operators because these are too complicated to be handled directly locally
 */

typedef uint8 PCLXL_TAG_TYPE;

typedef struct
{
  PCLXL_TAG         tag;      /* Tag seen, will be an operator or an attribute */
  PCLXL_ATTRIBUTE   attribute;
} PCLXL_STREAM_OBJECT_STRUCT;

typedef uint8 PCLXL_PARSER_STATE;

/* We scan a PCLXL data stream by reading unsigned byte codes which
 * are referred to as "tags" in PCLXL
 *
 * As we read these tags we interpret them as data types (with
 * associated data value(s) and attribute ID) and operators (with
 * optional following embedded data).
 *
 * Some of these operators represent begin and end markers of
 * sessions, pages, fonts and data sources.
 *
 * In order to read these byte-code tags, track the total number of
 * bytes read, record whether we are inside a session, page, font or
 * data source and the operator location in the event of an error we
 * maintain a "parser context" that is essentially passed around
 * everywhere within the "scan" code.
 *
 * Whenever we find a start of a session, page, font or data source we
 * will create a new parser context that copies the existing parser
 * context and then pushes this new context onto the top of a stack of
 * parser contexts
 *
 * Whenever we find the corresponding end of session, page. font or
 * data source we pop the top-level parser context from the stack,
 * update the parent parser context, which is now at the top of the
 * stack, with any updated information (like bytes read and latest
 * operator details) from the popped parser context which is then
 * deleted.
 *
 * We also use the parser context to capture/build-up the list of
 * attributes as they are encountered, ready to be used by an operator
 * (and then always discarded after the handling of the operator is
 * completed)
 */
typedef struct pclxl_parser_context_struct* PCLXL_PARSER_CONTEXT;

typedef struct pclxl_parser_context_struct
{
  PCLXL_CONTEXT                 pclxl_context;          /* back pointer to the root PCLXL "context" */

  sll_list_t                    stream_stack;           /* Stream stack - head is current stream */
  sll_list_t                    free_streams;           /* List of streams that can be used */
  PCLXLSTREAM                   streams[33];            /* XL streams */
  Bool                          data_source_open;       /* Has datasource been opened? */
  Bool                          data_source_big_endian; /* Endianness of datasource */

#define XL_MAX_STATES (16)
  int32                         parser_states_index;
  PCLXL_PARSER_STATE            parser_states[XL_MAX_STATES]; /* While parsing the PCLXL stream
                                                         * we encounter a number of "Begin..." operators
                                                         * with matching "End..." operators.
                                                         * In order to correctly validate the operator sequence
                                                         * we need to track these begins and ends so that we spot
                                                         * the operators that are only allowed between these begins and ends
                                                         * Therefore we use a simple "state machine" (see pclxlsmtable.{h,c})
                                                         * And this requires a simple stack of states
                                                         * which are implemented here as an array of (256) byte-states
                                                         * and an index into this array
                                                         */
  PCLXL_ATTRIBUTE_SET*          attr_set;               /* Set of attributes seen so far */
  Bool                          exit_parser;             /* Exit the current pclxl_scan() with success. */

  PCLXL_IMAGE_READ_CONTEXT      *image_reader;           /* Gets created when processing an image. */
  PCLXL_THRESHOLD_READ_CONTEXT  *threshold_reader;       /* Gets created when defining a dither threshold array. */

  /* The pattern currently in construction; this is only valid between the
   * start and end pattern operators. */
  PCLXL_PATTERN_CONTRUCTION_STATE pattern_construction;

  /* PassThrough state. */
  Bool                          doing_pass_through ;     /* Are we in the middle of doing a PassThrough command. */
  Bool                          last_command_was_pass_through ;
  PCLXL_EMBEDDED_READER         pass_through_reader;
  Bool                          cached_read_stream_object_result ;
  PCLXL_STREAM_OBJECT_STRUCT    cached_stream_object ;

} PCLXL_PARSER_CONTEXT_STRUCT;

PCLXL_PARSER_CONTEXT pclxl_create_parser_context(PCLXL_CONTEXT      pclxl_context,
                                                 PCLXL_PARSER_STATE parser_state);

void pclxl_delete_parser_context(PCLXL_PARSER_CONTEXT parser_context);

PCLXL_PARSER_CONTEXT pclxl_get_parser_context();

void set_exit_parser(PCLXL_PARSER_CONTEXT parser_context, Bool do_exit);

Bool pclxl_parser_push_stream(
  PCLXL_PARSER_CONTEXT  parser_context,
  FILELIST*             flptr);

void pclxl_parser_pop_stream(
  PCLXL_PARSER_CONTEXT  parser_context);

#define pclxl_parser_current_stream(p)  (SLL_GET_HEAD(&(p)->stream_stack, PCLXLSTREAM, next))
#define pclxl_parser_next_stream(p, s)  (SLL_GET_NEXT((s), PCLXLSTREAM, next))

#endif /* __PCLXLPARSERCONTEXT_H__ */

/******************************************************************************
* Log stripped */
