/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxlcontext.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief As the RIP processes a PCLXL data stream it maintains a
 * "context" that basically holds details of the current parser state
 * (which indludes the stream encoding/binding and bytes-read
 * counts)etc. and the current graphics state.
 */

#ifndef _PCLXLCONTEXT_H_
#define _PCLXLCONTEXT_H_ 1

#include "mm.h"
#include "lists.h"
#include "fileio.h"

#include "pcl.h"

#include "pclxldebug.h"
#include "pclxltypes.h"
#include "pclxlpatternt.h"
#include "pclxlgraphicsstatet.h"

/* There are a number of cross-dependencies between the various PCLXL
 * data structures that are most simply resolved using forward
 * references to selected structure pointers as follows:
 */

typedef struct pclxl_parser_context_struct* PCLXL_PARSER_CONTEXT_STACK;
typedef struct pclxl_error_info_struct*     PCLXL_ERROR_INFO_LIST;
typedef struct pclxl_context_struct*        PCLXL_CONTEXT;
typedef struct pclxl_attribute_struct*      PCLXL_ATTRIBUTE;
typedef struct PCLXL_ATTRIBUTE_SET          PCLXL_ATTRIBUTE_SET;

typedef struct pclxl_font_header_struct*    PCLXL_FONT_HEADER;
typedef struct pclxl_char_data_struct*      PCLXL_CHAR_DATA;
typedef struct PCLXLStreamCache             PCLXLStreamCache;

/* We are going to put minimal portability interface around the Core
 * RIP file/stream I/O API primarily to allow me to create a test
 * harness for the PCLXL-in development, but also to provide a degree
 * of shielding against changes to this Core RIP API
 *
 * The PCLXL stream API basically consists of a PCLXL_STREAM from
 * which individual uint8 bytes can be sequentially read and
 * "consumed" or read and then "unread" in reverse order to allow
 * bytes to be examined but not consumed.  I.e. getc(), ungetc() and
 * peekc()
 */

/**
 * \brief a PCLXL "context" structure is created for each PCLXL stream
 * that the pclxlexec operator is asked to process. It contains
 * *everything* that the PCLXL parser/handler needs access to.
 */

typedef struct pclxl_context_struct
{
  /** Per-thread core context structure */
  corecontext_t *corecontext ;

  /** All dynamically allocated structures, arrays etc. are allocated
   * from this "global" pool */
  mm_pool_t memory_pool;

  /** When the pclxlexec operator is invoked it is passed an stream and
   * optionally an "initialization" Postscript dictionary. In the
   * first instance this will contain stuff like the target
   * resolution. */
  OBJECT* init_params_dict;

  /** The parser "context" records all the state information associated
   * with the PCLXL operator stream including stream byte offsets,
   * counts and length.  it also includes details of the attributes
   * and last operator position etc.  We may only have one of these,
   * but the PCLXL_PARSER_CONTEXT structure supports a simple stack of
   * these structures */
  PCLXL_PARSER_CONTEXT_STACK parser_context;

  /** During the processing of a PCLXL job we maintain a current
   * graphics state and PCLXL operators support the creation and
   * deletion of a stack of graphics states. This is where we hold
   * this stack. */
  PCLXL_GRAPHICS_STATE_STACK graphics_state;

  /** There are several PCLXL "state" values that form part of the
   * PCLXL context but which are not part of the saveable and
   * restorable "graphics state" These items are stored beneath this
   * "non GS state" structure */
  PCLXL_NON_GS_STATE_STRUCT non_gs_state;

  /** PCLXL supports a number of error reporting modes which are
   * specified on a per-session basis */
  PCLXL_ErrorReport error_reporting;

  /** During processing of the PCLXL stream (and performing the
   * graphics operations therein) we can raise errors (and warnings)
   * about the content of the job We create PCLXL_ERROR_INFO structs
   * for each error and warning.  Errors are typically reported
   * immediately, but warnings are stored in a list and, in the
   * absence of an error are all reported at the end of the job */
  PCLXL_ERROR_INFO_LIST error_info_list;

  /** When opening and reading a font header the associated data source
   * is *always* bigendian Therefore BeginFontHeader always derives a
   * bigendian stream which ReadFontHeader reads the font header from.
   * The font header data is collected beneath this structure and the
   * data is used/consumed and the stream closed when the coresponding
   * EndFontHeader operator is encountered. */
  PCLXL_FONT_HEADER font_header;

  /** When reading a (soft-font) character the associated data source
   * is *always* bigendian Therefore BeginChar always derives a
   * bigendian stream from which the character data is read and
   * collected beneath this structure */
  PCLXL_CHAR_DATA char_data;

  /** Setup as a copy of the pclxl_config_params global which is set up
   * independently of any "pclxlexec" operator call by a call to
   * "setpclxlconfig" But this copy can then be overridden by a
   * further job-specific dictionary supplied to "pclxlexec" */
  PCLXL_CONFIG_PARAMS_STRUCT config_params;

  /** Raster pattern caches. */
  PCLXL_PATTERN_CACHES pattern_caches;

  /** User defined stream cache. */
  PCLXLStreamCache *stream_cache;

  /**
   * PCLXL to PCL[5] Passthrough state info
   * Created in response to a PCLPassthrough operator
   * and then retained and re-used for any subsequent PCLPassthrough operators
   * encountered within the same PCLXL job
   */
  PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* passthrough_state_info;

} PCLXL_CONTEXT_STRUCT;

Bool pclxl_create_context(corecontext_t *corecontext, OBJECT* config_params_dict,
                          PCLXL_CONTEXT *pclxl_context) ;

void pclxl_destroy_context(PCLXL_CONTEXT *old_pclxl_context) ;

PCLXL_CONTEXT pclxl_get_context(void);

#endif /* pclxlcontext_h */

/******************************************************************************
* Log stripped */
