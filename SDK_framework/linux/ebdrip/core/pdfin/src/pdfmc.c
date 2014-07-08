/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfmc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Marked Content Implemntation
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"

#include "pdfmc.h"
#include "pdfmem.h"
#include "pdfops.h"
#include "pdfin.h"
#include "namedef_.h"
#include "routedev.h"
#include "gstack.h"
#include "vndetect.h"

/* Prior to PDF1.5 we just ignored marked content operators. However,
   now these are used for optional content (via the OC tag). So we need
   to maintain a stack for the BMC/BDC operators popped using EMC.
     Currently we only intercept the OC (optional content) tag
*/


struct pdf_mc_stack_t
{
  int32 tag_name;
  Bool  oc_state;
  struct pdf_mc_stack_t * next;
};

static Bool change_optional_content_on(Bool val)
{
  optional_content_on = val;
  if (!flush_vignette( VD_Default ))
    return FALSE;

  return TRUE;
}

/* ---------------------------------------------------------------------- */

/* The marked-context operator is normally ignored except when the tag denotes
   optional content (OC). So we need to keep tabs on nesting - hence this mc stack. */
static Bool pdf_mc_pushitem(PDFCONTEXT *pdfc,OBJECT *tag, OBJECT * properties )
{
  pdf_mc_stack * item;
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;

  item = ( pdf_mc_stack * )mm_alloc( pdfxc->mm_structure_pool ,
          sizeof( pdf_mc_stack ) ,
          MM_ALLOC_CLASS_PDF_OC ) ;
  if ( !item )
    return error_handler( VMERROR ) ;

  item->tag_name = oNameNumber(*tag);
  item->oc_state = TRUE; /* Show opt content by default; applies to OC tags */
  item->next = imc->mc_stack;

  imc->mc_stack = item;

  if (properties != NULL) {
    switch (item->tag_name) {
     case NAME_OC:
      imc->mc_DP_oc_status = TRUE;
      if (!pdf_oc_getstate_fromprops(pdfc, properties, & item->oc_state)) {
        pdf_mc_freeall(imc, pdfxc->mm_structure_pool) ;
        return FALSE;
      }

      if (item->oc_state) {
        pdf_mc_stack * nitem;

        /* although the state is true, if it is nested inside
           a state that is false then it is also false */
        for (nitem = item->next;nitem;nitem = nitem->next) {
          if (nitem->tag_name == NAME_OC) {
            if (!nitem->oc_state) {
              item->oc_state = FALSE;
              break;
            }
          }
        }
      }

      if (!item->oc_state) {
        if (!change_optional_content_on(FALSE))
          return FALSE;
      }

      break;
    }
  }

  return TRUE;
}

static Bool pdf_mc_popitem(PDFCONTEXT *pdfc )
{
  pdf_mc_stack * item;
  pdf_mc_stack * next;
  PDF_IMC_PARAMS *imc ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  item = imc->mc_stack;
  if (item == NULL) {
    /* an end without a beginning (very Zen). This is
       bad PDF. But some Adobe RIPs seem happy so
       only fail if strict is on. */
    if (ixc->strictpdf)
      return error_handler( UNDEFINED ) ;
    else
      return TRUE;
  }

  next = imc->mc_stack = item->next;

  if (item->tag_name == NAME_OC) {
    pdf_mc_stack * nitem;
    Bool val = TRUE;

    /* find the last OC state or amke true */
    for (nitem = next;nitem;nitem = nitem->next) {
      if (nitem->tag_name == NAME_OC) {
        val = nitem->oc_state;
        break;
      }
    }

    if (!change_optional_content_on(val))
      return FALSE;
  }

  mm_free( pdfxc->mm_structure_pool,
          ( mm_addr_t )( item ) ,
          sizeof(pdf_mc_stack));

  return TRUE;
}

void pdf_mc_freeall(PDF_IMC_PARAMS *imc, mm_pool_t pool)
{
  pdf_mc_stack * item;

  while ((item = imc->mc_stack) != NULL) {
    imc->mc_stack = item->next;

    mm_free( pool,
            ( mm_addr_t )( item ) ,
            sizeof(pdf_mc_stack));
  }

  (void)change_optional_content_on(imc->mc_oc_initial_state);
}


Bool pdfop_BMC( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *tag ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( RANGECHECK ) ;

  tag = theITop( stack ) ;
  if ( oType(*tag) != ONAME )
    return error_handler( TYPECHECK ) ;

  if (!pdf_mc_pushitem(pdfc,tag, NULL))
    return FALSE;

  pop(stack) ;
  return TRUE ;
}

Bool pdfop_BDC( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *properties ;
  OBJECT *tag ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 1 )
    return error_handler( RANGECHECK ) ;

  tag = stackindex( 1 , stack ) ;
  if ( oType(*tag) != ONAME )
    return error_handler( TYPECHECK ) ;

  properties = theITop( stack ) ;
  if ( oType(*properties) != ODICTIONARY &&
       oType(*properties) != ONAME )
    return error_handler( TYPECHECK ) ;

  if (!pdf_mc_pushitem(pdfc,tag,properties))
    return FALSE;

  npop( 2 , stack ) ;
  return TRUE ;
}

Bool pdfop_EMC( PDFCONTEXT *pdfc )
{
  return pdf_mc_popitem(pdfc);
}

Bool pdfop_MP( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *theo ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 0 )
    return error_handler( RANGECHECK ) ;

  theo = theITop( stack ) ;
  if ( oType(*theo) != ONAME )
    return error_handler( TYPECHECK ) ;

  pop(stack) ;
  return TRUE ;
}

Bool pdfop_DP( PDFCONTEXT *pdfc )
{
  STACK *stack ;
  OBJECT *tag ;
  OBJECT *properties ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;

  stack = ( & imc->pdfstack ) ;
  if ( theIStackSize( stack ) < 1 )
    return error_handler( RANGECHECK ) ;

  tag = stackindex( 1 , stack ) ;
  if ( oType(*tag) != ONAME )
    return error_handler( TYPECHECK ) ;

  properties = theITop( stack ) ;
  if ( oType(*properties) != ODICTIONARY &&
       oType(*properties) != ONAME )
    return error_handler( TYPECHECK ) ;

  /* an OC tag will set the OC context to a given group
     directly. Currently this is only cleared by a similar
     tag or an OC tag from BDC
  */
  switch (oName(*tag) - system_names) {
    case NAME_OC:
      imc->mc_DP_oc_status = TRUE;
      if (!pdf_oc_getstate_fromprops(pdfc, properties, & imc->mc_DP_oc_status))
        return FALSE;

      if (! imc->mc_DP_oc_status) {
        if (!change_optional_content_on(FALSE))
          return FALSE;
      }
      break;
  }

  npop( 2 , stack ) ;
  return TRUE ;
}

/* end of file pdfmc.c */

/* Log stripped */
