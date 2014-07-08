/** \file
 * \ingroup pclxl
 *
 * $HopeName: COREpcl_pclxl!src:pclxldatasource.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 */

#include "core.h"

#include "pclxltypes.h"
#include "pclxldebug.h"
#include "pclxlcontext.h"
#include "pclxlerrors.h"
#include "pclxloperators.h"
#include "pclxlattributes.h"
#include "pclxlgraphicsstate.h"

/*
 * Tag 0x48 OpenDataSource
 */

Bool
pclxl_op_open_data_source(PCLXL_PARSER_CONTEXT parser_context)
{
  static PCLXL_ATTR_MATCH match[3] = {
#define OPENDATASOURCE_SOURCE_TYPE  (0)
    {PCLXL_AT_SourceType | PCLXL_ATTR_REQUIRED},
#define OPENDATASOURCE_DATA_ORG     (1)
    {PCLXL_AT_DataOrg | PCLXL_ATTR_REQUIRED},
    PCLXL_MATCH_END
  };
  static PCLXL_ENUMERATION allowed_data_sources[] = {
    PCLXL_eDefaultDataSource,
    PCLXL_ENUMERATION_END
  };
  static PCLXL_ENUMERATION allowed_data_orgs[] = {
    PCLXL_eBinaryHighByteFirst,
    PCLXL_eBinaryLowByteFirst,
    PCLXL_ENUMERATION_END
  };
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;
  PCLXL_DataSource data_source;
  PCLXL_DataOrg data_org;

  /* Check for allowed attributes and data types */
  if ( !pclxl_attr_set_match(parser_context->attr_set, match, pclxl_context,
                             PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  /* SourceType */
  if ( !pclxl_attr_match_enumeration(match[OPENDATASOURCE_SOURCE_TYPE].result, allowed_data_sources,
                                     &data_source, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }
  /* DataOrg */
  if ( !pclxl_attr_match_enumeration(match[OPENDATASOURCE_DATA_ORG].result, allowed_data_orgs,
                                     &data_org, pclxl_context, PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  if ( parser_context->data_source_open ) {
    /* CET E111.bin expects an error, FTS T107.bin does not fail with this */
    PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_STATE, PCLXL_DATA_SOURCE_NOT_CLOSED,
                        ("There is already an open data source. Should not open more than one data source at the same time"));
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("OpenDataSource"));

  parser_context->data_source_open = TRUE;
  parser_context->data_source_big_endian = (data_org == PCLXL_eBinaryHighByteFirst);

  return TRUE;
}

/*
 * Tag 0x49 CloseDataSource
 */

Bool
pclxl_op_close_data_source(PCLXL_PARSER_CONTEXT parser_context)
{
  PCLXL_CONTEXT pclxl_context = parser_context->pclxl_context;

  if ( !pclxl_attr_set_match_empty(parser_context->attr_set, pclxl_context,
                                   PCLXL_SS_KERNEL) ) {
    return(FALSE);
  }

  PCLXL_DEBUG(PCLXL_DEBUG_OPERATORS, ("CloseDataSource"));

  if ( !parser_context->data_source_open ) {
    /*
     * There is no open data source stream So there is nothing to be closed So
     * the only question is: Is this an error or just a warning We need to check
     * this with a real printer (like the HP4700)
     *
     * Unfortunately although the PCLXL Protocol Class Specification says that
     * this an error, it looks like closing an un-opened data source is a no-op
     * i.e. is silently ignored
     */

    if ( pclxl_context->config_params.strict_pclxl_protocol_class ) {
      PCLXL_ERROR_HANDLER(pclxl_context, PCLXL_SS_KERNEL, PCLXL_DATA_SOURCE_NOT_OPEN,
                          ("There is no open data source to close"));
      return FALSE;
    }
  }
  /*
   * We need to close the currently open data source and set the
   * data_source_stream back to NULL
   */
  parser_context->data_source_open = FALSE;
  return TRUE;
}

/******************************************************************************
* Log stripped */
