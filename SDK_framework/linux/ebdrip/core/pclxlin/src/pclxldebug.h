/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxldebug.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Some simple debug function(s) to allow
 * conditionally compiled-in, runtime-switchable diagnostics
 * to be logged from within the PCLXL code
 * Note also that the debug function should *always* ensure that
 * the traced output ends in a "newline"
 * Also contains a PCLXL config params structure and "setpclxldefaults"
 * Postscript operator.
 */

#ifndef __PCLXLDEBUG_H__
#define __PCLXLDEBUG_H__ 1

#include "objectt.h"
#include "swdevice.h"

#include "pclxltypes.h"

#define PCLXL_STREAM_ENDIANNESS_LITTLE 0x01
#define PCLXL_STREAM_ENDIANNESS_BIG    0x02
#define PCLXL_STREAM_ENDIANNESS_BOTH   (PCLXL_STREAM_ENDIANNESS_LITTLE | PCLXL_STREAM_ENDIANNESS_BIG)

typedef struct pclxl_config_params
{
  Bool                    strict_pclxl_protocol_class;
  uint8                   stream_endianness_supported;
  uint8                   default_font_name[33];
  uint32                  default_font_name_len;
  uint32                  courier_weight;
  PCLXL_SysVal            default_point_size;
  uint32                  default_symbol_set;
  PCLXL_Orientation       default_orientation;
  Bool                    default_duplex;
  PCLXL_DuplexPageMode    default_duplex_binding;
  PCLXL_MediaSize         default_media_size;
  PCLXL_MediaSource       default_media_source;
  PCLXL_MediaDestination  default_media_destination;
  uint8                   default_media_type[128];
  uint32                  default_media_type_len;
  uint32                  default_page_copies;
  uint8                   backchannel_filename[LONGESTDEVICENAME + LONGESTFILENAME + 2];
  uint32                  backchannel_filename_len;
  OBJECT*                 job_config_dict;
  int32                   vds_select ;

#ifdef DEBUG_BUILD

  int32   debug_pclxl;

#endif

} PCLXL_CONFIG_PARAMS_STRUCT, *PCLXL_CONFIG_PARAMS;

extern PCLXL_CONFIG_PARAMS_STRUCT pclxl_config_params;

extern Bool pclxl_config_params_init(void);

extern Bool
pclxl_set_config_params(OBJECT*             config_params_dict,
                        PCLXL_CONFIG_PARAMS config_params,
                        uint8*              called_from_location);

#ifdef DEBUG_BUILD

/*
 * Note that the long-term plan is to use HQTRACE() to output all debug information
 * So that it is integrated with all other subsystems' debug output
 * However HQTRACE() is currently rather "noisy" in its output
 * So I am continuing to use pclxl_debug() which is pretty trivial
 * and was actually implemented before I discovered that HQTRACE() existed
 * and/or thought that HQTRACE() messages could not include parameters
 *
#define PCLXL_DEBUG(_DEBUG_COND_, _DEBUG_MSG_) HQTRACE(((_DEBUG_COND_) & pclxl_config_params.debug_pclxl), _DEBUG_MSG_)
 */
#define PCLXL_DEBUG(_DEBUG_COND_, _DEBUG_MSG_) if ((_DEBUG_COND_) & pclxl_config_params.debug_pclxl) pclxl_debug _DEBUG_MSG_ ;

extern void pclxl_debug(char* format, ...);

#define PCLXL_DEBUG_POSTSCRIPT_OPS  0x00000001
#define PCLXL_DEBUG_TAGS            0x00000002
#define PCLXL_DEBUG_DATA_TYPES      0x00000004
#define PCLXL_DEBUG_ATTRIBUTES      0x00000008

#define PCLXL_DEBUG_OPERATORS       0x00000010
#define PCLXL_DEBUG_ENUMERATIONS    0x00000020
#define PCLXL_DEBUG_DATA_LENGTH     0x00000040
#define PCLXL_DEBUG_DATA_VALUES     0x00000080

#define PCLXL_DEBUG_EMBEDDED_DATA   0x00000100
#define PCLXL_DEBUG_INITIALIZATION  0x00000200
#define PCLXL_DEBUG_ENCODING        0x00000400
#define PCLXL_DEBUG_STREAM_HEADER   0x00000800

#define PCLXL_DEBUG_EMBEDDED_DATA2  0x00001000
#define PCLXL_DEBUG_COMMENTS        0x00002000
#define PCLXL_DEBUG_MM_GRID_OUTPUT  0x00004000
#define PCLXL_DEBUG_STATE_MACHINE   0x00008000

#define PCLXL_DEBUG_COLOR           0x00010000
#define PCLXL_DEBUG_PATHS           0x00020000
#define PCLXL_DEBUG_ARC_PATH        0x00040000
#define PCLXL_DEBUG_GS_STACK        0x00080000

#define PCLXL_DEBUG_FONTS           0x00100000
#define PCLXL_DEBUG_ERRORS          0x00200000
#define PCLXL_DEBUG_WARNINGS        0x00400000
#define PCLXL_DEBUG_BYTES_READ      0x00800000

#define PCLXL_DEBUG_ARRAY_VALUES    0x01000000
#define PCLXL_DEBUG_PCL5_OPS        0x02000000
#define PCLXL_DEBUG_RASTERS         0x04000000
#define PCLXL_DEBUG_CURSOR_POSITION 0x08000000

#define PCLXL_DEBUG_PAGE_CTM        0x10000000
#define PCLXL_DEBUG_PLOTCHAR        0x20000000
#define PCLXL_DEBUG_SUBSYSTEM       0x40000000

#define PCLXL_DEBUG_DO_NOT_USE      0x80000000

#else  /* DEBUG_BUILD */

#define PCLXL_DEBUG(_DEBUG_COND_, _DEBUG_MSG_)

#endif /* DEBUG_BUILD */

#endif /* __PCLXLDEBUG_H__ */

/******************************************************************************
* Log stripped */
