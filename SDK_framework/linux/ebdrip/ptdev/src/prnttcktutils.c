/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWptdev!src:prnttcktutils.c(EBDSDK_P.1) $
 */

#include "ptincs.h"
#include "prnttckt.h"
#include "prnttcktutils.h"
#include "dynstring.h"


/**
 * @file
 * @brief Print ticket handling utility functions.
 */

/**
 *  Append a PostScript-safe copy of the specified string to the record.
 *
 *  The following characters will be escaped: '(', ')', '\'
 *
 *  @param[in,out] s  The string to append to
 *  @param[in] ptbz   The string to append
 */
static void PSSafeString (PSString * s, const char* const ptbz)
{
  const char* p = ptbz;

  while (*p)
  {
    if (*p == '(' || *p == ')' || *p == '\\')
      PSStringAppendChar( s, '\\' );

    PSStringAppendChar( s, *p );

    p ++;
  }
}

/**
 *  Iterate over the PT_PARAM tree of which pRefParam belongs, looking
 *  for the target ParameterInit being referenced.
 *
 *  @param pRefParam  The ParameterRef reference
 *  @return a pointer to the children of the referenced ParameterInit (or
 *  NULL if not found).
 */
static PT_PARAM* getParamFromReference (const PT_PARAM* const pRefParam)
{
  const PT_PARAM* pRoot;
  PT_PARAM* pParam;

  /* Find root object */
  pRoot = pRefParam;
  while (pRoot && pRoot->p_parent)
    pRoot = pRoot->p_parent;

  /* Search for named ParameterInit */
  pParam = pt_find_sub_param ((PT_PARAM*) pRoot,
    PT_PARAM_PARAMETERINIT,
    pRefParam->name->localpart);
  if (pParam && pParam->p_children)
    return pParam->p_children;

  /* Target was not found */
  return NULL;
}

/**
 *  Produce a PostScript representation of the specified Print Ticket
 *  parameter.  If child objects are found then they are recursed.
 *
 *  The \c nLevel parameter is used to filter the Print Ticket. For example,
 *  if \c PT_LEVEL_JOB is specified only job-level settings are included
 *  in the output string (E.g. 'JobNUp', 'JobDuplex', etc).
 *
 *  @param[in,out] s   The string to append to
 *  @param[in] pParam  Pointer to the Print Ticket parameter
 *  @param[in] nLevel  A PT_LEVEL_xxx value.
 */
static void pt_paramToPS (PSString * s, PT_PARAM* pParam, int32 nLevel)
{
  while ( pParam != NULL )
  {
    int bWriteArrayOp = FALSE;
    int bHasContent = FALSE;

    PT_PARAM* pParamToWrite;
    PT_PARAM* pChild;

    if (pParam->param_type == PT_PARAM_PARAMETERREF)
      pParamToWrite = getParamFromReference (pParam);
    else
      pParamToWrite = pParam;

    pChild = pParamToWrite ? pParamToWrite->p_children : NULL;

    if (pParamToWrite)
    {
      int bWriteParamName = (pParamToWrite->param_type != PT_PARAM_UNDEFINED && pParamToWrite->name);
      int bWriteValue = (pParamToWrite->value_type != PT_PARAM_UNDEFINED);

      /* Filter out features we are not interested in */
      int32 nParamLevel = pt_param_level (pParamToWrite);
      if (nParamLevel != PT_LEVEL_UNKNOWN && nParamLevel != nLevel)
      {
        /* Do not process feature or its children */
        bHasContent = FALSE;
        pChild = NULL;
      }
      else
      {
        bHasContent = (bWriteParamName || bWriteValue);
      }

      if (bHasContent)
      {
        /* Write param name */
        if (bWriteParamName)
        {
          if (pParamToWrite->param_type == PT_PARAM_OPTION)
            PSStringAppendString( s, "/Value " );
          else if (pChild)
            bWriteArrayOp = TRUE;

          PSStringAppendChar( s, '/' );
          PSStringAppendString( s, (char*) pParamToWrite->name->localpart );
          PSStringAppendChar( s, ' ' );
        }

        if (bWriteArrayOp)
          PSStringAppendString( s, "<<" );

        /* Write value */
        if (bWriteValue)
        {
          switch (pParamToWrite->value_type)
          {
          case PT_VALUE_STRING:
            PSStringAppendString( s, "/Value (" );
            PSSafeString( s, (char*) pParamToWrite->value.string );
            PSStringAppendChar( s, ')' );
            break;
          case PT_VALUE_DECIMAL:
            PSStringAppendString( s, "/Value " );
            PSStringAppendDouble( s, pParamToWrite->value.decimal );
            break;
          case PT_VALUE_INTEGER:
            PSStringAppendString( s, "/Value " );
            PSStringAppendInt( s, pParamToWrite->value.integer );
            break;
          case PT_VALUE_QNAME:
            PSStringAppendString( s, "/Value (" );
            PSSafeString( s, (char*) pParamToWrite->value.qname->localpart );
            PSStringAppendChar( s, ')' );
            break;
          default:
            /* Unknown/unhandled type */
            break;
          }
        }
      }
    }

    if (pChild)
      pt_paramToPS (s, pChild, nLevel);

    if (bHasContent)
    {
      if (bWriteArrayOp)
        PSStringAppendString( s, ">>" );
    }

    pParam = pParam->p_sibling;
  }
}

/**
 *  Append an array of doubles as a PostScript representation of the
 *  form "[n1 n2 n3 ...]" to a record.
 *
 *  @param[in,out] s   The string to append to
 *  @param[in] pArr    The input array
 *  @param[in] nElems  The number of numbers in the array
 */
static void doubleArrayToPS (PSString * s, const double* const pArr, uint32 nElems)
{
  uint32 i;

  PSStringAppendChar( s, '[' );

  for (i = 0; i < nElems; i ++)
  {
    PSStringAppendChar( s, ' ' );
    PSStringAppendDouble( s, pArr[i] );
  }

  PSStringAppendChar( s, ']' );
}

char* pt_createJobStartPSFromPTParam (struct PT_PARAM* pParam)
{
  PSString* s;
  char* pbz = NULL;

  if (PSStringOpen (&s))
  {
    PSStringAppendString( s, "/HqnXPSPrintTicket /ProcSet findresource /StartJob get <<" );
    pt_paramToPS (s, pParam, PT_LEVEL_JOB);
    PSStringAppendString( s, ">> exch exec\n" );

    pbz = PSStringCopyBuffer( s );

    PSStringClose( s );
  }

  return pbz;
}

char* pt_createDocumentStartPSFromPTParam (struct PT_PARAM* pParam)
{
  PSString* s;
  char * pbz = NULL;

  if (PSStringOpen (&s))
  {
    PSStringAppendString( s, "/HqnXPSPrintTicket /ProcSet findresource /StartDocument get <<" );
    pt_paramToPS (s, pParam, PT_LEVEL_DOCUMENT);
    PSStringAppendString( s, ">> exch exec\n" );

    pbz = PSStringCopyBuffer( s );

    PSStringClose( s );
  }

  return pbz;
}

char* pt_createPageStartPSFromPTParam (const struct PT_PAGEAREAS* pPageAreas,
                                       int fXPSSignatureValid,
                                       struct PT_PARAM* pParam)
{
  PSString* s;
  char * pbz = NULL;

  if (PSStringOpen (&s))
  {
    PSStringAppendString( s, "/HqnXPSPrintTicket /ProcSet findresource /StartPage get " );
    PSStringAppendString( s, "<< /FixedPage " );
    doubleArrayToPS (s, pPageAreas->arrFixedPageSize, 2);
    PSStringAppendString( s, " /BleedBox " );
    doubleArrayToPS (s, pPageAreas->arrBleedBox, 4);
    PSStringAppendString( s, " /ContentBox " );
    doubleArrayToPS (s, pPageAreas->arrContentBox, 4);
    PSStringAppendString( s, " /ImageableArea " );
    doubleArrayToPS (s, pPageAreas->arrImageableArea, 4);
    PSStringAppendString( s, " /TrimToImageableArea " );
    PSStringAppendString( s, pPageAreas->fTrimToImageableArea ? "true" : "false" );
    PSStringAppendString( s, ">> " );
    PSStringAppendString( s, fXPSSignatureValid ? "true" : "false" );
    PSStringAppendString( s, " <<" );
    pt_paramToPS (s, pParam, PT_LEVEL_PAGE);
    PSStringAppendString( s, ">>\n" );
    PSStringAppendString( s, "4 -1 roll exec\n" );

    pbz = PSStringCopyBuffer( s );

    PSStringClose( s );
  }

  return pbz;
}


