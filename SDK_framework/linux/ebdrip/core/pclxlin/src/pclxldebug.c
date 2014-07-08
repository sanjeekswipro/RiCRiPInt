/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxldebug.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Some simple debug function(s) to allow
 * conditionally compiled-in, runtime-switchable diagnostics
 * to be logged from within the PCLXL code
 */

#include <stdarg.h>

#include "core.h"
#include "fileio.h"
#include "swcopyf.h"
#include "hqmemset.h"
#include "hqmemcpy.h"
#include "monitor.h"
#include "dictscan.h"   /* NAMETYPEMATCH */
#include "namedef_.h"   /* NAME_strict_pclxl_protocol_class, NAME_debug_pclxl etc.*/
#include "stacks.h"     /* operandstack */
#include "swerrors.h"   /* error_handler() */
#include "ripdebug.h"   /* register_ripvar() */

#include "pclxldebug.h"
#include "pclxlerrors.h"

/**
 * \brief pclxl_set_config_params() takes a Postscript dictionary object
 * (possibly but not necessarily from the global operandstack)
 *
 * It validates that the dictionary contents is valid
 * and then extracts the dictionary contents into
 * a caller-supplied PCLXL_CONFIG_PARAMS structure
 * which *may* be the global "pclxl_config_params"
 * or *may* be a *copy* of this global's contents
 * that is held beneath a PCLXL_CONTEXT structure
 */

Bool
pclxl_set_config_params(OBJECT*             config_params_dict,
                        PCLXL_CONFIG_PARAMS config_params,
                        uint8*              called_from_location)
{
  enum {
    STRICT_PCLXL_PROTOCOL_CLASS = 0,
    STREAM_ENDIANNESS,

    PCLXL_DEFAULT_FONT_NAME,
    COURIER_WEIGHT,
    POINT_SIZE,
    SYMBOL_SET,

    COPIES,
    BACK_CHANNEL,
    VIRTUAL_DEVICE_SPACE,

#ifdef DEBUG_BUILD

    DEBUG_PCLXL,

#endif

    PCLXL_CONFIG_PARAMS_END_INDEX
  };

  static NAMETYPEMATCH match[PCLXL_CONFIG_PARAMS_END_INDEX + 1] =
  {
    {NAME_Strict | OOPTIONAL, 1, {OBOOLEAN}},
    {NAME_StreamEndianness | OOPTIONAL, 1, {ONAME}},

    {NAME_PCLXLDefaultFontName | OOPTIONAL, 2, {OSTRING, ONAME}},
    {NAME_Courier | OOPTIONAL, 1, {OINTEGER}},
    {NAME_PointSize | OOPTIONAL, 2, {OREAL, OINTEGER}},
    {NAME_SymbolSet | OOPTIONAL, 1, {OINTEGER}},

    {NAME_Copies | OOPTIONAL, 1, {OINTEGER}},
    {NAME_BackChannel | OOPTIONAL, 1, {OSTRING}},
    {NAME_VirtualDeviceSpace | OOPTIONAL, 1, {ODICTIONARY}},

#ifdef DEBUG_BUILD

    {NAME_DebugPCLXL | OOPTIONAL, 1, {OINTEGER}},

#endif

    DUMMY_END_MATCH
  };

  OBJECT* o_strict = NULL;
  OBJECT* o_stream_endianness = NULL;

  OBJECT* o_default_font_name = NULL;
  OBJECT* o_courier_weight = NULL;
  OBJECT* o_point_size = NULL;
  OBJECT* o_symbol_set = NULL;

  OBJECT* o_default_page_copies = NULL;
  OBJECT* o_backchannel_filename = NULL;
  OBJECT* o_vds = NULL;

#ifdef DEBUG_BUILD

  OBJECT* o_debug_pclxl = NULL;

#endif

  if ( !oCanRead(*oDict(*config_params_dict)) &&
       !object_access_override(oDict(*config_params_dict)) )
  {
    return error_handler(INVALIDACCESS);
  }

  /*
   * Check that the dictionary entries
   * match the entries that we are expecting
   */

  if ( !dictmatch(config_params_dict, match) )
  {
    return FALSE; /* dictmatch() will already have raised any TYPECHECK errors */
  }

  /*
   * Look for and "unpack" the dictionary entries
   * into the PCLXL_CONTEXT's pclxl_config_params structure
   */

  if ( ((o_strict = match[STRICT_PCLXL_PROTOCOL_CLASS].result) != NULL) )
  {
    config_params->strict_pclxl_protocol_class = oBool(*o_strict);
  }

  if ( ((o_stream_endianness = match[STREAM_ENDIANNESS].result) != NULL) )
  {
    switch ( oName(*o_stream_endianness)->namenumber )
    {
    case NAME_Little:

      config_params->stream_endianness_supported = PCLXL_STREAM_ENDIANNESS_LITTLE;

      break;

    case NAME_Big:

      config_params->stream_endianness_supported = PCLXL_STREAM_ENDIANNESS_BIG;

      break;

    case NAME_Both:

      config_params->stream_endianness_supported = PCLXL_STREAM_ENDIANNESS_BOTH;

      break;

    default:

      return error_handler(RANGECHECK);

      break;
    }
  }

  if ( ((o_default_font_name = match[PCLXL_DEFAULT_FONT_NAME].result) != NULL) )
  {
    uint8* default_font_name = NULL;
    uint32 default_font_name_len = 0;
    uint32 default_font_name_max_len = sizeof(config_params->default_font_name);

    if ( oType(*o_default_font_name) == ONAME )
    {
      NAMECACHE* cached_name = oName(*o_default_font_name);

      default_font_name_len = cached_name->len;

      if ( default_font_name_len > default_font_name_max_len )
      {
        return error_handler(RANGECHECK);
      }
      else
      {
        default_font_name = cached_name->clist;
      }
    }
    else /* It must be an OSTRING */
    {
      default_font_name_len = theLen(*o_default_font_name);

      if ( default_font_name_len > default_font_name_max_len )
      {
        return error_handler(RANGECHECK);
      }
      else
      {
        default_font_name = oString(*o_default_font_name);
      }
    }

    HqMemCpy(config_params->default_font_name,
             default_font_name,
             default_font_name_len);

    config_params->default_font_name_len = default_font_name_len;

    if ( default_font_name_len < default_font_name_max_len )
      HqMemZero(&config_params->default_font_name[default_font_name_len],
                (default_font_name_max_len - default_font_name_len));
  }

  if ( ((o_courier_weight = match[COURIER_WEIGHT].result) != NULL) )
  {
    int32 courier_weight = oInteger(*o_courier_weight);

    if ( (courier_weight >= 0) &&
         (courier_weight <= 1) )
    {
      config_params->courier_weight = courier_weight;
    }
    else
    {
      return error_handler(RANGECHECK);
    }
  }

  if ( ((o_point_size = match[POINT_SIZE].result) != NULL) )
  {
    PCLXL_SysVal default_point_size;

    if ( oType(*o_point_size) == OREAL )
    {
      default_point_size = oReal(*o_point_size);
    }
    else
    {
      default_point_size = (PCLXL_SysVal) oInteger(*o_point_size);
    }

    if ( default_point_size > 0.0 )
    {
      config_params->default_point_size = default_point_size;
    }
    else
    {
      return error_handler(RANGECHECK);
    }
  }

  if ( ((o_symbol_set = match[SYMBOL_SET].result) != NULL) )
  {
    int32 default_symbol_set = oInteger(*o_symbol_set);

    if ( (default_symbol_set >= 0) &&
         (default_symbol_set <= 32767) )
    {
      config_params->default_symbol_set = (uint32) default_symbol_set;
    }
    else
    {
      return error_handler(RANGECHECK);
    }
  }

  if ( ((o_default_page_copies = match[COPIES].result) != NULL) )
  {
    int32 default_page_copies = oInteger(*o_default_page_copies);

    if ( default_page_copies >= 0 )
    {
      config_params->default_page_copies = (uint32) default_page_copies;
    }
    else
    {
      return error_handler(RANGECHECK);
    }
  }

  if ( ((o_backchannel_filename = match[BACK_CHANNEL].result) != NULL) )
  {
    uint32 backchannel_filename_len = theLen(*o_backchannel_filename);

    if ( backchannel_filename_len > sizeof(config_params->backchannel_filename) )
    {
      return error_handler(RANGECHECK);
    }
    else
    {
      HqMemCpy(config_params->backchannel_filename,
               oString(*o_backchannel_filename),
               backchannel_filename_len);

      config_params->backchannel_filename_len = backchannel_filename_len;

      if ( backchannel_filename_len < sizeof(config_params->backchannel_filename) )
        HqMemZero(&config_params->backchannel_filename[backchannel_filename_len],
                  (sizeof(config_params->backchannel_filename) - backchannel_filename_len));
    }
  }

  if ( (o_vds = match[VIRTUAL_DEVICE_SPACE].result) != NULL ) {
    if ( !pcl_param_vds_select(o_vds, &config_params->vds_select) )
      return FALSE ;
  }

#ifdef DEBUG_BUILD

  if ( ((o_debug_pclxl = match[DEBUG_PCLXL].result) != NULL) )
  {
    config_params->debug_pclxl = oInteger(*o_debug_pclxl);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_INITIALIZATION,
              ("\n\
<<\n\
\t%s\t\t/Strict                       %s\n\
\t%s\t\t/StreamEndianness             %d\n\
\t%s\t\t/PCLXLDefaultFontName         (%s)\n\
\t%s\t\t/Courier                      %d\n\
\t%s\t\t/PointSize                    %f\n\
\t%s\t\t/SymbolSet                    %d\n\
\t%s\t\t/Copies                       %d\n\
\t%s\t\t/BackChannel                  (%s)\n\
\t%s\t\t/DebugPCLXL                   %d\n\
\tcurrentpagedevice\t/PCLDefaultOrientation        %d\n\
\tcurrentpagedevice\t/PCLDefaultDuplex             %s\n\
\tcurrentpagedevice\t/PCLDefaultTumble             %s\n\
\tcurrentpagedevice\t/PCLXLDefaultPageSize         %d\n\
\tcurrentpagedevice\t/PCLXLDefaultMediaSource      %d\n\
\tcurrentpagedevice\t/PCLXLDefaultMediaDestination %d\n\
\tcurrentpagedevice\t/PCLXLDefaultMediaType        (%s)\n\
>> %s\n",
               called_from_location, (config_params->strict_pclxl_protocol_class ? "true" : "false"),
               called_from_location, config_params->stream_endianness_supported,
               called_from_location, config_params->default_font_name,
               called_from_location, config_params->courier_weight,
               called_from_location, config_params->default_point_size,
               called_from_location, config_params->default_symbol_set,
               called_from_location, config_params->default_page_copies,
               called_from_location, config_params->backchannel_filename,
               called_from_location, config_params->debug_pclxl,
               config_params->default_orientation,
               (config_params->default_duplex ? "true" : "false"),
               (config_params->default_duplex_binding == PCLXL_eDuplexHorizontalBinding ? "true" : "false"),
               config_params->default_media_size,
               config_params->default_media_source,
               config_params->default_media_destination,
               config_params->default_media_type,
               called_from_location));

#else

  UNUSED_PARAM(uint8*, called_from_location);

#endif

  return TRUE;
}

PCLXL_CONFIG_PARAMS_STRUCT pclxl_config_params = { 0 };

/*
 * pclxl_setpclxlparams_call_count counts the number of times
 * that setpclxlparams_() PostScript operator is called
 * because we only actually allow this operator to modify the configuration
 * behind the first call to this function
 */

uint32 pclxl_setpclxlparams_call_count = 0;

void init_C_globals_pclxldebug(void)
{
  PCLXL_CONFIG_PARAMS_STRUCT config_param_defaults = {
    FALSE,
    PCLXL_STREAM_ENDIANNESS_LITTLE,

    "Courier         ",
    16,
    0,
    12,
    341,  /* = PC_8 as taken from HP4700 rather than the ROMAN8 from the technical reference */
    PCLXL_ePortraitOrientation,
    FALSE,
    PCLXL_eDuplexVerticalBinding,
    PCLXL_eLetterPaper,
    PCLXL_eAutoSelect,
    PCLXL_eFaceDownBin,
    "",
    0,
    1,
    "",
    0,
    NULL,
    PCL_VDS_INIT
#ifdef DEBUG_BUILD
    ,0
#endif
  };

  pclxl_config_params = config_param_defaults;

  pclxl_setpclxlparams_call_count = 0;
}

Bool pclxl_config_params_init(void)
{
#ifdef DEBUG_BUILD
  register_ripvar(NAME_DebugPCLXL, OINTEGER, &pclxl_config_params.debug_pclxl);

  PCLXL_DEBUG(PCLXL_DEBUG_INITIALIZATION, ("Initialized PCLXL configuration param \"debug_pclxl\" to 0"));
#endif

  return TRUE;
}

/**
 * \brief setpclxlparams_() implements the Postscript "setpclxlparams" operator
 * which takes a dictionary containing "configuration" parameters
 * for the PCLXL interpreter/rip
 *
 * It expects a single dictionary on the operand stack
 * which it "consumes" if this is a valid PCLXL "configuration" parameters dictionary
 *
 * The contents of this dictionary are transfered into/stored in
 * the "pclxl_config_params" global variable which is entirely
 * separate/independent from any PCLXL_CONTEXT struct
 * whose existence only lasts for the duration of a single call to "pclxlexec"
 *
 * However it is expected that a *copy* of the global pclxl_config_params
 * is taken into the PCLXL_CONTEXT structure
 * and this copy may be further modified by the contents of a second
 * pclxl configuration parameters dictionary that is supplied to the "pclxlexec" operator.
 *
 * These latter additional or overriding configuration items
 * only operate for the duration of the pclxlexec operation.
 *
 * But this does mean that the remainder of the PCLXL code
 * is supposed to access any/all PCLXL configuration items
 * via a parent PCLXL_CONTEXT structure rather than
 * via the pclxl_config_params global
 */

Bool setpclxlparams_(ps_context_t *pscontext)
{
  OBJECT* odict;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /*
   * Check for readable dictionary on the stack
   */

  if ( isEmpty(operandstack) )
  {
    return error_handler(STACKUNDERFLOW) ;
  }

  if ( oType(*(odict = theTop(operandstack))) != ODICTIONARY )
  {
    return error_handler(TYPECHECK);
  }

  if ( pclxl_setpclxlparams_call_count++ )
  {
#ifdef DEBUG_BUILD

    static char* ordinal_suffix[10] = {
      "th",
      "st",
      "nd",
      "rd",
      "th",
      "th",
      "th",
      "th",
      "th",
      "th"
    };

    PCLXL_DEBUG(PCLXL_DEBUG_INITIALIZATION,
                ("setpclxlparams_() only works once (and this is the %d%s call)",
                 pclxl_setpclxlparams_call_count,
                 ordinal_suffix[(pclxl_setpclxlparams_call_count % 10)]));

 #endif

    pop(&operandstack);

    return TRUE;
  }

  if ( !pclxl_set_config_params(odict,
                                &pclxl_config_params,
                                (uint8*) "setpclxlparams") )
  {
    return error_handler(CONFIGURATIONERROR);
  }
  else
  {
    pop(&operandstack);

    return TRUE ;
  }
}

/**
 * \brief
 * pclxl_debug() provides a simple varargs interface and
 * much like printf() and monitorf() it accepts a format string
 * followed by zero or more values to be inserted according to the format.
 *
 * It is basically a wrapper around a call to monitorf() that
 * is both conditionally compiled-in and is also run-time switchable
 * under the control of one (or more bits within) pclxl_config_params.debug_pclxl
 *
 * Note that pclxl_config_params.debug_pclxl is a *signed* int32 because it is
 * mapped as a Postscript "ripvar" and so must al
 */

#ifdef DEBUG_BUILD

void
pclxl_debug(char* format, ...)
{
  /*
   * Yes, we're in the compiled-in branch
   * and at least one of the bits set in the debuf "mask"
   * is also set in the pclxl_config_params.debug_pclxl ripvar
   * So we go ahead and output this debug message to "monitor"
   *
   * Note that we would like to use vmonitorf()
   * so that it would handle our vargs argument list
   * but it is not published.
   * So we have to do our own formatting using vswncopyf()
   */

  char buffer[1000];   /* Same arbitary buffer size is also hard-coded
                        * into vmonitorf() in core/src/glue.c
                        * It would actually be nice if this limit was published
                        */

  size_t l;

  va_list argp;

  va_start(argp, format);

  (void) vswncopyf((uint8*) buffer,
                   (sizeof(buffer) - 2),  /* 2 byte reserved for a trailing
                                           * newline and '\0' byte if necessary
                                           */
                   (uint8*) format,
                   argp);

  va_end(argp);

  /*
   * Before we call monitorf()
   * let's just ensure that the formatted debug string
   * ends
   */

  if ( ((l = strlen(buffer)) > 0) && /* It is safe to access buffer[(l - 1)] */
       (buffer[(l - 1)] != '\n') &&  /* The buffer does not already end in a newline */
       (l < (sizeof(buffer) - 2)) )  /* There is room for (at least) one more character + terminating '\0' byte */
  {
    buffer[l++] = '\n';
    buffer[l] = '\0';
  }

  monitorf((uint8*) "Debug: %s", buffer);
}

#endif /* DEBUG_BUILD */

/******************************************************************************
* Log stripped */
