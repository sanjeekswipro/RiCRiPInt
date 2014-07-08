/* Copyright (C) 2006-2013 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:prnttckt.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

#include <stdlib.h>
#include <string.h>

#include "ptincs.h"

#include "prnttckt.h"
#include "prnttcktutils.h"


/**
 * @file
 * @brief Print ticket handling.
 */

/** @brief The PrintTicket context structure */
struct PRINTTICKET {
  int32         refcnt;       /**< Reference count so can be shared across scopes */
  PT_PARAM      root;         /**< PT root element */
  PT_PARAM*     p_current;    /**< Param currently being added to */
};



static
void pt_reset(
  PRINTTICKET*  pt)
{
  pt->refcnt = 1;

  /* Set current element to root - new elements will be added as children */
  pt->p_current = &pt->root;

  /* Root element will never have a parent or siblings, just children */
  pt->root.p_parent = pt->root.p_sibling =
    pt->root.p_children = pt->root.p_last_child = NULL;
  pt->root.name = NULL;

  /* The root element has no meaningful parameter associated with it */
  pt->root.param_type = PT_PARAM_UNDEFINED;
  pt->root.value_type = PT_VALUE_UNDEFINED;
}


int32 pt_initialise(
  PRINTTICKET** p_pt)
{
  PRINTTICKET*  pt;

  pt = (PRINTTICKET *) MemAlloc(sizeof(PRINTTICKET), FALSE, FALSE);
  if ( pt == NULL ) {
    return(FALSE);
  }

  pt_reset(pt);
  *p_pt = pt;
  return(TRUE);
}

static
PT_PARAM* pt_new_param(
  int32         param_type,
  XML_QNAME*    name)
{
  PT_PARAM* param;

   /* Allocate new print ticket parameter structure and initialise it */
  param = (PT_PARAM *) MemAlloc(sizeof(PT_PARAM), FALSE, FALSE);
  if ( param != NULL ) {
    /* Set name of parameter */
    param->name = name;

    /* Set type of parameter and initialise value is underfined */
    param->param_type = param_type;
    param->value_type = PT_PARAM_UNDEFINED;
    param->value.integer = 0;

    param->p_sibling = param->p_children = param->p_last_child = NULL;
  }
  return(param);
}

static
void pt_release_param(
  PT_PARAM**  p_param)
{
  PT_PARAM* param;

  param = *p_param;

  /* No more references to parameter so release resources */
  if ( param->name != NULL ) {
    xml_qname_free(&param->name);
  }
  if ( (param->value_type == PT_VALUE_STRING) &&
       (param->value.string != NULL) ) {
    MemFree(param->value.string);
    param->value.string = NULL;
  }
  if ( (param->value_type == PT_VALUE_QNAME) &&
       (param->value.string != NULL) ) {
    xml_qname_free(&param->value.qname);
  }
  MemFree(param);

  *p_param = NULL;
}

static
void pt_add_param(
  PRINTTICKET*  pt,
  PT_PARAM*     param)
{
  /* The parent is the current element, there are no children or later siblings,
   * but it is the latest child of the parent. */
  param->p_parent = pt->p_current;
  if ( pt->p_current->p_last_child != NULL ) {
    pt->p_current->p_last_child->p_sibling = param;
  } else {
    pt->p_current->p_children = param;
  }
  pt->p_current->p_last_child = param;
}

int32 pt_add_new_param(
  PRINTTICKET*  pt,
  int32         param_type,
  XML_QNAME*    name)
{
  PT_PARAM* param;

  /* Create new parameter and add to PrintTicket */
  param = pt_new_param(param_type, name);
  if ( param == NULL ) {
      return(FALSE);
    }
  pt_add_param(pt, param);

  /* New parameter is now the current print ticket parameter being added to */
  pt->p_current = param;
  return(TRUE);
}

void pt_end_param(
  PRINTTICKET*  pt)
{
  pt->p_current = pt->p_current->p_parent;
}


int32 pt_set_value(
  PRINTTICKET*  pt,
  int32         value_type,
  void*         value)
{
  PT_PARAM*   p_param;

  p_param = pt->p_current;

  p_param->value_type = value_type;
  switch ( p_param->value_type ) {
  case PT_VALUE_INTEGER:
    p_param->value.integer = *(int32*)value;
    break;

  case PT_VALUE_DECIMAL:
    p_param->value.decimal = *(double*)value;
    break;

  case PT_VALUE_QNAME:
    p_param->value.qname = (XML_QNAME*)value;
    break;

  case PT_VALUE_STRING:
    /* Take copy of string passed in */
    p_param->value.string = utl_strdup((uint8*)value);
    if ( p_param->value.string == NULL ) {
      return(FALSE);
    }
    break;
  }
  return(TRUE);
}


/** @brief Release all child parameters recursively */
static
void pt_purge_children(
  PT_PARAM* p_parent)
{
  PT_PARAM* p_child;
  PT_PARAM* p_next;

  /* Depth first walk releasing parameters */
  p_child = p_parent->p_children;
  while ( p_child != NULL ) {
    if ( p_child->p_children != NULL ) {
      pt_purge_children(p_child);
    }

    p_next = p_child->p_sibling;
    pt_release_param(&p_child);
    p_child = p_next;
  }
  p_parent->p_children = p_parent->p_last_child = NULL;
}


int32 pt_complete(
  PRINTTICKET*  pt)
{
  return(pt->p_current == &pt->root);
}


PRINTTICKET* pt_copy(
  PRINTTICKET*  pt)
{
  /* One more reference to the PrintTicket */
  pt->refcnt++;
  return(pt);
}

/* Free off a transferred print ticket */
void pt_release(
  PRINTTICKET** p_pt)
{
  PRINTTICKET*  pt;

  pt = *p_pt;
  if ( --pt->refcnt == 0 ) {
    /* No more references, purge PrintTicket content before freeing the print ticket */
    pt_purge_children(&pt->root);
    MemFree(pt);
  }
  *p_pt = NULL;
}


typedef struct LEVEL_PREFIX {
  uint8*  name;
  int32   len;
} LEVEL_PREFIX;

static LEVEL_PREFIX prefix[3] = {
  {STRING_AND_LENGTH("Job")},
  {STRING_AND_LENGTH("Document")},
  {STRING_AND_LENGTH("Page")}
};

int32 pt_param_level(
  PT_PARAM* param)
{
  int32 level;
  HqBool fProcessParam = FALSE;

  /* Process features/parameters */
  if ( (param->param_type == PT_PARAM_FEATURE) ||
       (param->param_type == PT_PARAM_PARAMETERINIT) )
  {
    fProcessParam = TRUE;
  }
  else
  {
    /* Also process top-level properties */
    if ((param->param_type == PT_PARAM_PROPERTY) && param->p_parent->name == NULL)
      fProcessParam = TRUE;
  }

  /* Find feature/parameter init level based on prefix */
  if ( fProcessParam ) {
    for ( level = PT_LEVEL_JOB; level <= PT_LEVEL_PAGE; level++ ) {
      if ( strncmp((char*)param->name->localpart,
                   (char*)prefix[level].name, CAST_SIGNED_TO_SIZET(prefix[level].len)) == 0 ) {
        return(level);
      }
    }
  }
  return(PT_LEVEL_UNKNOWN);
}


/** @brief Return first child of PrintTicket structure */
static
PT_PARAM* pt_first_child(
  PT_PARAM* child)
{
  return(child->p_children);
}

/** @brief Return sibling of PrintTicket structure */
static
PT_PARAM* pt_next_sibling(
  PT_PARAM* child)
{
  return(child->p_sibling);
}

static
int32 pt_add_copy_param(
  PRINTTICKET*  pt,
  PT_PARAM*     param)
{
  PT_PARAM*   child;
  PT_PARAM*   param_new;
  XML_QNAME*  qname;

  /* Add copy of parameter to PrintTicket (parameter may not have a name) */
  qname = NULL;
  if ( param->name != NULL ) {
    qname = xml_qname_copy(param->name);
    if ( qname == NULL ) {
      return(FALSE);
    }
  }
  param_new = pt_new_param(param->param_type, qname);
  if ( param_new == NULL ) {
    if ( qname != NULL ) {
      xml_qname_free(&qname);
    }
    return(FALSE);
  }
  pt_add_param(pt, param_new);

  /* Copy parameter value */
  param_new->value_type = param->value_type;
  switch ( param_new->value_type ) {
  case PT_VALUE_INTEGER:
    param_new->value.integer = param->value.integer;
    break;

  case PT_VALUE_DECIMAL:
    param_new->value.decimal = param->value.decimal;
    break;

  case PT_VALUE_QNAME:
    /* Take copy of qname */
    param_new->value.qname = xml_qname_copy(param->value.qname);
    if ( param_new->value.qname == NULL ) {
      return(FALSE);
    }
    break;

  case PT_VALUE_STRING:
    /* Take copy of string */
    param_new->value.string = utl_strdup(param->value.string);
    if ( param_new->value.string == NULL ) {
      return(FALSE);
    }
    break;
  }

  /* Deep copy children of parameter copied */
  pt->p_current = param_new;

  child = pt_first_child(param);
  while ( child != NULL ) {
    if ( !pt_add_copy_param(pt, child) ) {
      return(FALSE);
    }
    child = pt_next_sibling(child);
  }

  pt->p_current = pt->p_current->p_parent;

  return(TRUE);
}

static
PT_PARAM* pt_has_feature(
  PRINTTICKET*  pt,
  XML_QNAME*    name)
{
  PT_PARAM* param;

  /* Loop over root's children looking for a match on the name */
  param = pt_first_child(&pt->root);
  while ( param != NULL ) {
    if ( param->name && xml_qname_cmp(param->name, name) == 0 )  {
      return(param);
    }
    param = pt_next_sibling(param);
  }
  return(NULL);
}


int32 pt_merge_and_validate(
  PRINTTICKET** p_pt,
  PRINTTICKET*  pt_valid,
  PRINTTICKET*  pt_new,
  int32         level)
{
  int32     param_level;
  PT_PARAM* param;
  PT_PARAM* param_new;

  if ( pt_new == NULL ) {
    /* No PrintTicket to merge, new validated PrintTicket is copy of original
     * validated PrintTicket */
    *p_pt = pt_copy(pt_valid);
    return(TRUE);
  }

  /* Merge the validated PrintTicket with the new PrintTicket.
   * For each parameter in the validated PrintTicket, if it can be overridden by
   * the new PrintTicket parameter that is present, copy in the parameter from
   * the new PrintTicket, else copy in the parameter from the validated
   * PrintTicket.
   */
  pt_initialise(p_pt);
  param = pt_first_child(&pt_valid->root);
  while ( param != NULL ) {

    param_new = NULL;
    param_level = pt_param_level(param);
    if ( param_level >= level ) {
      param_new = pt_has_feature(pt_new, param->name);
    }
    if ( !pt_add_copy_param(*p_pt, (param_new != NULL ? param_new : param)) ) {
      return(FALSE);
    }

    param = pt_next_sibling(param);
  }

  /* Copy parameters which only exist in the new PrintTicket. */
  param = pt_first_child(&pt_new->root);
  while ( param != NULL ) {
    param_new = NULL;
    param_level = pt_param_level(param);
    if ( param_level >= level ) {
      if ( !pt_has_feature(pt_valid, param->name) ) {
        if ( !pt_add_copy_param(*p_pt, param) ) {
          return(FALSE);
        }
      }
    }
    param = pt_next_sibling(param);
  }


  return(TRUE);
}

/**
 * @brief Find parameter element of given type and name (can be null), optionally
 * recursing into contained elements. */
static
PT_PARAM* pt_find_element(
  PT_PARAM* param,
  int32     type,
  uint8*    name,
  int32     recurse)
{
  PT_PARAM* child;
  PT_PARAM* grand_child;

  child = pt_first_child(param);
  while ( child != NULL ) {
    /* Return sub element if it matches type and name if requested */
    if ( (child->param_type == type) &&
         ((name == NULL) ||
          (strcmp((char*)child->name->localpart, (char*)name) == 0)) ) {
      return(child);
    }

    /* Check sub elements of this element if recursing */
    if ( recurse ) {
      grand_child = pt_find_element(child, type, name, TRUE);
      if ( grand_child != NULL ) {
        return(grand_child);
      }
    }

    /* Check next element at same level */
    child = pt_next_sibling(child);
  }

  return(NULL);
}

PT_PARAM* pt_find_feature(
  PRINTTICKET*  pt,
  uint8*        name)
{
  return(pt_find_element(&pt->root, PT_PARAM_FEATURE, name, FALSE));
}

PT_PARAM* pt_find_paraminit(
  PRINTTICKET*  pt,
  uint8*        name)
{
  return(pt_find_element(&pt->root, PT_PARAM_PARAMETERINIT, name, FALSE));
}

PT_PARAM* pt_find_sub_param(
  PT_PARAM* param,
  int32     type,
  uint8*    name)
{
  return(pt_find_element(param, type, name, TRUE));
}

int32 pt_param_value_type(
  PT_PARAM* param)
{
  return(param->value_type);
}

void pt_param_value(
  PT_PARAM* param,
  void*     value)
{
  switch ( param->value_type ) {
  case PT_VALUE_INTEGER:
    *(int32*)value = param->value.integer;
    break;

  case PT_VALUE_DECIMAL:
    *(double*)value = param->value.decimal;
    break;

  case PT_VALUE_QNAME:
    *(XML_QNAME**)value = param->value.qname;
    break;

  case PT_VALUE_STRING:
    *(uint8**)value = param->value.string;
    break;
  }
}

int32 pt_param_has_children(PRINTTICKET* pt)
{
  HQASSERT(pt != NULL, "No print ticket");
  return (pt_first_child (pt->p_current) != NULL);
}

int32 pt_param_has_valid_children(PRINTTICKET* pt, int32* pnValidChildren)
{
  PT_PARAM* pParamChild;

  HQASSERT(pt != NULL, "No print ticket");
  HQASSERT(pnValidChildren != NULL, "Nowhere to store valid children");

  /* Check each child has the correct type */
  pParamChild = pt_first_child (pt->p_current);
  while (pParamChild)
  {
    int32 fHasCorrectType = FALSE;

    int32* pn = pnValidChildren;
    while (*pn != PT_PARAM_CHECK_END)
    {
      if (pParamChild->param_type == *pn)
      {
        fHasCorrectType = TRUE;
        break;
      }
      pn ++;
    }

    /* Return FALSE as soon as an invalid child is found */
    if (! fHasCorrectType)
      return FALSE;

    pParamChild = pt_next_sibling (pParamChild);
  }

  return TRUE;
}

int32 pt_param_has_required_children (PRINTTICKET* pt, int32 nCondition, int32* pnValidChildren)
{
  int32 nRequiredChildren = 0;
  PT_PARAM* pParamChild;

  HQASSERT(pt != NULL, "No print ticket");
  HQASSERT(nCondition == PT_ONE_OF || nCondition == PT_ONE_OR_MORE,
           "Print ticket condition wrong");
  HQASSERT(pnValidChildren != NULL,
           "Nowhere to store number of valid children");

  /* Count number of 'required' children */
  pParamChild = pt_first_child (pt->p_current);
  while (pParamChild)
  {
    int32* pn = pnValidChildren;
    while (*pn != PT_PARAM_CHECK_END)
    {
      if (pParamChild->param_type == *pn)
      {
        nRequiredChildren ++;
        break;
      }
      pn ++;
    }
    pParamChild = pt_next_sibling (pParamChild);
  }

  if (nCondition == PT_ONE_OF)
    return (nRequiredChildren == 1);
  return (nRequiredChildren >= 1);
}

/**
 * @brief Create PostScript which will issue a PrintTicket error message and
 * stop execution.
 *
 * @see pt_createJobStartPS()
 * @see pt_createDocumentStartPS()
 * @see pt_createPageStartPS()
 */
static uint8* pt_createNULLPrintTicketPS ()
{
  uint8* ptbzStopPS =
    (uint8*) "(%%[ Error: Cannot process an invalid \\(NULL\\) PrintTicket. ]%%) =\n"
             "stop\n";
  return utl_strdup (ptbzStopPS);
}

uint8* pt_createPageStartPS (const PT_PAGEAREAS* pPageAreas,
                             int32 fXPSSignatureValid,
                             PRINTTICKET* pTicket)
{
  if (! pTicket)
    return pt_createNULLPrintTicketPS ();

  return (uint8*) pt_createPageStartPSFromPTParam (pPageAreas,
                                                   fXPSSignatureValid,
                                                   &pTicket->root);
}

uint8* pt_createJobStartPS (PRINTTICKET* pTicket)
{
  if (! pTicket)
    return pt_createNULLPrintTicketPS ();

  return (uint8*) pt_createJobStartPSFromPTParam (&pTicket->root);
}

uint8* pt_createDocumentStartPS (PRINTTICKET* pTicket)
{
  if (! pTicket)
    return pt_createNULLPrintTicketPS ();

  return (uint8*) pt_createDocumentStartPSFromPTParam (&pTicket->root);
}


/* EOF prnttckt.c */
