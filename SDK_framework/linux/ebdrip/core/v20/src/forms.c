/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:forms.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2013-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to handle PostScript Forms.
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "dictscan.h"
#include "swpdfin.h"
#include "namedef_.h"

#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "dlstate.h"
#include "stacks.h"
#include "constant.h"

#include "hdl.h"
#include "routedev.h"
#include "dl_bbox.h"
#include "dl_free.h"

#include "dicthash.h"
#include "gstack.h"
#include "control.h"
#include "pathcons.h"
#include "plotops.h"                /* degenerateClipping */
#include "gu_rect.h"
#include "gu_ctm.h"
#include "utils.h"
#include "hdlPrivate.h"             /* HDL functionality */

#include "params.h"
#include "psvm.h"
#include "swmemory.h"
#include "miscops.h"
#include "typeops.h"

#include "idlom.h"
#include "forms.h"
#include "vndetect.h"
#include "dl_store.h"


/* Keep track of the innermost form HDL under construction, and preserve it and
   all its parents that are forms, because none of them are on their parent HDLs
   yet. */

static HDL *form_hdl = NULL;


Bool in_execform(void)
{
  return form_hdl != NULL;
}


void preserve_execform(DL_STATE* page)
{
  HDL *hdl;

  for ( hdl = form_hdl ; hdlPurpose(hdl) == HDL_FORM ; hdl = hdlParent(hdl) )
    /* There can't be any intervening ones that aren't forms, because groups
       don't allow partial paints, and the others can't contain forms. In any
       case, if we add some, they'd need their own mechanism of preservation. */
    dlSSPreserve(page->stores.hdl, hdlStoreEntry(hdl), TRUE);
}


/** Execute the given paintproc, storing the results in an HDL.
 *
 * An explicit newpath not needed since one is done by cliprectangles. */
static Bool form_exec_paintproc( corecontext_t *context ,
                                 OBJECT *formdict ,
                                 int32 formId ,
                                 OBJECT *oPaintProc ,
                                 OMATRIX *ctm ,
                                 sbbox_t *bbox ,
                                 HDL **hdl )
{
  Bool result = FALSE ;
  RECTANGLE cliprect ;
  HDL *new_hdl ;
  int32 gid ;
  int saved_dl_safe_recursion = dl_safe_recursion;
  dl_erase_nr eraseno_before = context->page->eraseno;
  Bool had_erase = FALSE;

  *hdl = NULL ;

  if ( ! gs_gpush( GST_FORM ))
    return FALSE ;

  if ( ! DEVICE_SETG( context->page, GSC_FILL , DEVICE_SETG_NORMAL ))
    return FALSE ;

  /* Now we've done the gsave, we must do the corresponding restore so
   * when an error crops up, set result rather than just returning. */
#define return DO_NOT_return_SET_result_INSTEAD!

  gid = gstackptr->gId ;

  /* Take a local copy of the form dict for the HDLT callbacks to
   * refer to.  The formdict is on the top of the stack on entry, the
   * callback procedure will consume it, but HDLT needs a reference to
   * the object to hang onto between the begin and end target
   * callbacks. */
  if ( push(formdict, &temporarystack) ) {
    formdict = theTop(temporarystack) ;

    bbox_to_rectangle( bbox , & cliprect ) ;

    /* In much the same way as for patterns, set both the device and
     * page matrix so that a form paintproc gets something sensible
     * when it goes "matrix setmatrix". */

    if ( gs_setdefaultctm(ctm, FALSE) &&
         gs_setctm(ctm, FALSE) &&
         IDLOM_BEGINFORM(formdict, formId) != IB_PSErr ) {
      /* Having done IDLOM_BEGINFORM(), we must do IDLOM_ENDFORM. */

      if ( cliprectangles(&cliprect, 1) &&
           gs_gpush(GST_GSAVE) ) {
        dlc_context_t *dlc_context = context->page->dlc_context;
        dl_color_t *dlc_current, saved_color ;
        uint8 saved_spflags = dl_currentspflags(dlc_context) ;
        uint8 saved_exflags = dl_currentexflags ;
        uint8 saved_disposition = dl_currentdisposition;
        COLORVALUE saved_opacity = dl_currentopacity(dlc_context) ;
        HDL *saved_targetHdl = context->page->targetHdl ; /* hdlClose() clears the target HDL */
        Bool saved_degenerateClipping = degenerateClipping ;

        dlc_current = dlc_currentcolor(dlc_context);
        dlc_copy_release(dlc_context, &saved_color,
                         dlc_current) ;

        /* The assumption here is that an array or packed array
         * PaintProc means execute it as PS, but a file PaintProc must
         * be a PDF stream (in which case it must have a nonzero PDF
         * context ID - pdf_exec_stream checks for this). */

        if ( hdlOpen(context->page, TRUE, HDL_FORM, &new_hdl) ) {
          HDL *previous_form_hdl = form_hdl;
          int current_dl_safe_recursion = dl_safe_recursion;

          dl_safe_recursion = saved_dl_safe_recursion;
          form_hdl = new_hdl;

          if ( oType( *oPaintProc ) == OFILE ) {
            FILELIST *flptr = oFile( *oPaintProc ) ;
            HQASSERT( flptr , "flptr NULL in form_exec_paintproc.\n" ) ;
            if ( isIOpenFileFilter( oPaintProc , flptr ) &&
                 isIInputFile( flptr ))
              result = pdf_exec_stream( oPaintProc , PDF_STREAMTYPE_FORM );
            else
              result = error_handler( IOERROR ) ;
          } else {
            if ( push( oPaintProc , & executionstack ))
              result = interpreter( 1 , NULL ) ;
          }

          form_hdl = previous_form_hdl;
          dl_safe_recursion = current_dl_safe_recursion;

          /* eraseno will have changed if something fairly catastrophic occured
           * during the execution of the PS for the form paintproc. This can
           * include a failing partial-paint (triggering a page erase on
           * tidy-up), or a direct erasepage call. A successful partial paint
           * will NOT change eraseno, as it will be of type DL_ERASE_PRESERVE
           * which is defined to maintain eraseno. Such a catastrophic change
           * means that all the state we saved before the paintproc call may
           * have been deleted or gone out of scope. In particular the hdl
           * we created will have been deleted by the erase, so what we now
           * have is a dangling pointer. In such cases must not try and restore
           * any state or close the hdl, just get out as quick as we can.
           */
          if ( eraseno_before != context->page->eraseno )
            had_erase = TRUE;

          if ( !had_erase ) {
            if ( ! hdlClose( & new_hdl , result ))
              result = FALSE ;

            if ( result ) {
              if ( hdlIsEmpty(new_hdl) )
                hdlDestroy( & new_hdl ) ;
              else
                *hdl = new_hdl ;
            }
          }
        }
        if ( !had_erase ) {
          degenerateClipping = saved_degenerateClipping ;
          context->page->targetHdl = saved_targetHdl ;
          dl_set_currentopacity(dlc_context, saved_opacity) ;
          dl_currentdisposition = saved_disposition ;
          dl_currentexflags = saved_exflags ;
          dl_set_currentspflags(dlc_context, saved_spflags) ;
          dlc_release(dlc_context, dlc_current) ;
          dlc_copy_release(dlc_context, dlc_current, &saved_color) ;
        }
      }

      if (! IDLOM_ENDFORM(result))
        result = FALSE;
    }

    formdict = NULL ;
    pop(&temporarystack) ;
  }

  if ( ! gs_cleargstates( gid , GST_FORM , NULL )) {
    result = FALSE ;
  }

#undef return
  return result ;
}

/* ----------------------------------------------------------------------------
   function:            execform_()                   author:   John Sturdy
   creation date:       1-July-1991        last modification:   ##-###-####
   arguments:           argdict
   description:

   Operator described on page 174, PS-2.

---------------------------------------------------------------------------- */

Bool execform_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);

  if ( ! context->systemparams->PostScript )
    return error_handler( INVALIDACCESS ) ;

  return gs_execform( context, & operandstack ) ;
}

Bool gs_execform( corecontext_t *context, STACK *stack )
{
  USERPARAMS *userparams = context->userparams;
  Bool result = FALSE ;
  OBJECT *theo ;
  OBJECT *thed ;
  OMATRIX matrix ;
  OMATRIX ctm ;
  RECTANGLE cliprect ;
  sbbox_t bbox ;
  HDL *hdl = NULL ;
  int32 formId ;
  int32 saved_RecombineObject = userparams->RecombineObject;

  static int32 form_uid = 0 ;

  enum { form_FormType, form_XUID, form_BBox, form_Matrix, form_PaintProc,
         form_Implementation, form_dummy } ;
  static NAMETYPEMATCH formdictmatch[form_dummy + 1] = {
    { NAME_FormType, 1, { OINTEGER }},
    { NAME_XUID | OOPTIONAL, 2, { OARRAY | CANREAD, OPACKEDARRAY | CANREAD }},
    { NAME_BBox, 2, { OARRAY | CANREAD, OPACKEDARRAY | CANREAD }},
    { NAME_Matrix, 2, { OARRAY | CANREAD, OPACKEDARRAY | CANREAD }},
    { NAME_PaintProc, 3, { OARRAY | CANEXEC, OPACKEDARRAY | CANEXEC , OFILE | CANEXEC}},
    { NAME_Implementation | OOPTIONAL, 1, { OINTEGER }},
    DUMMY_END_MATCH
    } ;

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( isEmpty( *stack ))
    return error_handler( STACKUNDERFLOW ) ;

  /* Check for a dictionary on the top of the stack. */
  theo = theITop( stack ) ;
  if ( oType(*theo) != ODICTIONARY)
    return error_handler( TYPECHECK ) ;

  thed = oDict( *theo ) ;
  if ( ! oCanRead( *thed ))
    if ( ! object_access_override( thed ))
      return error_handler( INVALIDACCESS ) ;

  if ( ! dictmatch( theo , formdictmatch ))
    return error_handler( RANGECHECK ) ;

  /* FormType must be 1 */
  if ( oInteger(*formdictmatch[form_FormType].result) != 1 )
    return error_handler( TYPECHECK ) ;

  if ( !object_get_bbox(formdictmatch[form_BBox].result, &bbox) )
    return FALSE ;

  bbox_to_rectangle(&bbox, &cliprect) ;

  if ( ! is_matrix(formdictmatch[form_Matrix].result, &matrix) )
    return FALSE ;
  matrix_mult( & matrix , & thegsPageCTM( *gstateptr ) , & ctm ) ;

  /* Write the Implementation field if appropriate. Deliberately
     side-steps permissions checking by calling fast_insert_hash,
     since that's what it appears other RIPs do. See 28997. */
  if ( ! formdictmatch[form_Implementation].result ) {
    oInteger(inewobj) = formId = ++form_uid ;
    if ( !fast_insert_hash_name(theo, NAME_Implementation, &inewobj) )
      return FALSE ;
  } else {
    formId = oInteger(*formdictmatch[form_Implementation].result) ;
  }

  /* Make the dictionary read-only */
  if ( !reduceOaccess( READ_ONLY , TRUE , theo ))
    return FALSE ;

  /* Disable object-recombine around the form and the HDL lobj.
     Object-recombine doesn't work with sub-HDLs. */
  userparams->RecombineObject = 0;
#define return USE_goto_cleanup!

  if ( !form_exec_paintproc( context, theo , formId ,
                             formdictmatch[ form_PaintProc ].result ,
                             & ctm , & bbox , & hdl ) )
    goto cleanup ;

  /** \todo call IDLOM_FORM(formId, ...) to cause callback for form object
      (NYI). */

  if ( hdl != NULL ) {
    dbbox_t bbox;

    hdlBBox(hdl, &bbox);

    if ( bbox_is_empty(&bbox) )
      hdlDestroy(&hdl);
    else {
      LISTOBJECT *lobj;
      dl_color_t *dlc_current;
      Bool setg_result;
      int32 gid;

      if ( !gs_gpush(GST_FORM) )
        goto cleanup;
      gid = gstackptr->gId ;

      /* The prevailing transparency state applies to the sub-elements of the HDL;
         the HDL listobject itself must be opaque. */
      tsDefault(&gstateptr->tranState, gstateptr->colorInfo);

      setg_result = DEVICE_SETG( context->page, GSC_FILL , DEVICE_SETG_GROUP );

      if ( !gs_cleargstates(gid, GST_FORM, NULL) ||
           !setg_result )
        goto cleanup;

      /* Copy the HDL color (the merged dl color of the HDL's contents) into
         dlc_currentcolor, ready to be used for the HDL lobj's dl color. */
      dlc_current = dlc_currentcolor(context->page->dlc_context);
      dlc_release(context->page->dlc_context, dlc_current);
      if ( !dlc_copy(context->page->dlc_context,
                     dlc_current /* dst */, hdlColor(hdl) /* src */) ||
           !make_listobject(context->page, RENDER_hdl, &bbox, &lobj) )
        goto cleanup;

      lobj->dldata.hdl = hdl;
      lobj->spflags |= RENDER_KNOCKOUT;
      lobj->spflags &= ~RENDER_PATTERN;

      /* The HDL may contain a mixture of recombine-intercepted objects and some
         not, but if any are present then RENDER_RECOMBINE needs setting to
         ensure the dl color is eventually remapped. */
      if ( hdlRecombined(hdl) )
        lobj->spflags |= RENDER_RECOMBINE;

      if ( !add_listobject(context->page, lobj, NULL) ) {
        free_listobject(lobj, context->page);
        goto cleanup;
      }
    }
  }

  result = TRUE;
 cleanup:
  userparams->RecombineObject = saved_RecombineObject;
  if ( !result && hdl != NULL )
    hdlDestroy(&hdl);
#undef return
  return result;
}

/*
Log stripped */
