/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfattrs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Attributes access implementation
 */

#include "core.h"

#include "swcopyf.h"  /* swcopyf */
#include "swerrors.h" /* error_handler */

#include "constant.h" /* EPSILON */
#include "dictscan.h" /* NAMETYPEMATCH */
#include "gstack.h"   /* gstateptr */
#include "miscops.h"  /* run_ps_string */
#include "pdfxref.h"  /* pdf_lookupxref */

#include "pdfin.h"    /* pdf_ixc_params */
#include "swpdfin.h"  /* pdfin_xcontext_base */

#include "pdfattrs.h" /* PDF_PAGEDEV */
#include "pdfdefs.h"  /* PDF_PAGECROPTO_ARTBOX */
#include "pdfmatch.h" /* pdf_dictmatch */
#include "pdfrr.h"    /* pdf_rr_setpagedevice */

#include "namedef_.h"

/* compare_boxes() is given two arrays which are required to be
   four elements each, all containing numbers (integers or reals).
   They are checked for equality, allowing for floating point
   rounding problems.  Note that the general routine 'compare_objects()'
   should not be used in this case since that routine tests for
   exact equality between OREALs.
*/
static Bool compare_boxes( OBJECT *pArr1, OBJECT *pArr2 )
{
  sbbox_t bbox1, bbox2 ;

  /* One of the arguments may have been given as ONULL. */
  if (oType(*pArr1) != oType(*pArr2))
    return FALSE;

  if ( object_get_bbox(pArr1, &bbox1) &&
       object_get_bbox(pArr2, &bbox2) ) {
    SYSTEMVALUE *bbindex1, *bbindex2 ;
    uint32 i ;

    bbox_as_indexed(bbindex1, &bbox1) ;
    bbox_as_indexed(bbindex2, &bbox2) ;

    /* Compare relative errors of all components. */
    for ( i = 0 ; i < 4 ; ++i )
      if ( fabs(bbindex1[i] - bbindex2[i]) > max(fabs(bbindex1[i]),
                                                 fabs(bbindex2[i])) * EPSILON )
        return FALSE ;

    return TRUE ;
  }

  HQFAIL("Malformed bounding box") ;
  return FALSE ;
}


/* ---------------------------------------------------------------------- */

/* Determines if the setpagedevice operator needs to be called for the
 * current page. A new device will be installed if the orientation or
 * PDF page boundary attributes change.
 */
enum { cropbox_cropbox = 0, cropbox_artbox,  cropbox_trimbox,
       cropbox_bleedbox,    cropbox_mediabox, cropbox_userunit,
       cropbox_max};
static NAMETYPEMATCH cropbox_dict[cropbox_max + 1] = {
/* 0 */ { NAME_CropBox  | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
/* 1 */ { NAME_ArtBox   | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
/* 2 */ { NAME_TrimBox  | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
/* 3 */ { NAME_BleedBox | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
/* 4 */ { NAME_MediaBox | OOPTIONAL , 3 , { ONULL, OARRAY, OPACKEDARRAY }},
/* 5 */ { NAME_UserUnit | OOPTIONAL , 3 , { OINTEGER, OREAL, OINDIRECT  }},
          DUMMY_END_MATCH
} ;

static Bool pdf_dosetpagedevice( PDF_PAGEDEV *pagedev, int32 orientation )
{
  OBJECT *pdevdict = & theIgsDevicePageDict( gstateptr ) ;
  OBJECT *theo ;

  HQASSERT( pdevdict, "pdevdict is NULL in pdf_dosetpagedevice" ) ;
  HQASSERT( oType(*pdevdict) == ODICTIONARY,
            "pdevdict not a dictionary in pdf_dosetpagedevice" ) ;

  /* Determine the page orientation currently installed */
  if ( ( theo = fast_extract_hash_name( pdevdict, NAME_Orientation ) ) == NULL )
    return TRUE ;

  HQASSERT( oType(*theo) == OINTEGER,
            "Orientation key not an integer in pdf_dosetpagedevice" ) ;

  /* If the page orientation has changed, then a new page device must
   * be installed.
   */
  if (oInteger(*theo) != orientation )
    return TRUE ;

  /* Determine the page boundary attributes currently installed */
  if ( ( theo = fast_extract_hash_name( pdevdict, NAME_CropBox ) ) == NULL )
    return TRUE ;

  HQASSERT( oType(*theo) == ODICTIONARY,
            "Cropbox key not a dictionary in pdf_dosetpagedevice" ) ;

  if ( ! dictmatch( theo, cropbox_dict ) )
    return TRUE ;

  /* Check if the new /CropBox dictionary matches the current dictionary
   * installed in the page device. If it does not, then a new page device
   * must be installed.
   */
  if ( cropbox_dict[cropbox_cropbox].result ) {
    if ( ! pagedev->CropBox )
      return TRUE ;
    if ( ! compare_boxes( cropbox_dict[cropbox_cropbox].result, pagedev->CropBox ) )
      return TRUE ;
  }
  else {
    if ( pagedev->CropBox )
      return TRUE ;
  }

  if ( cropbox_dict[cropbox_artbox].result ) {
    if ( ! pagedev->ArtBox )
      return TRUE ;
    if ( ! compare_boxes( cropbox_dict[cropbox_artbox].result, pagedev->ArtBox ) )
      return TRUE ;
  }
  else {
    if ( pagedev->ArtBox )
      return TRUE ;
  }

  if ( cropbox_dict[cropbox_trimbox].result ) {
    if ( ! pagedev->TrimBox )
      return TRUE ;
    if ( ! compare_boxes( cropbox_dict[cropbox_trimbox].result, pagedev->TrimBox ) )
      return TRUE ;
  }
  else {
    if ( pagedev->TrimBox )
      return TRUE ;
  }

  if ( cropbox_dict[cropbox_bleedbox].result ) {
    if ( ! pagedev->BleedBox )
      return TRUE ;
    if ( ! compare_boxes( cropbox_dict[cropbox_bleedbox].result, pagedev->BleedBox ) )
      return TRUE ;
  }
  else {
    if ( pagedev->BleedBox )
      return TRUE ;
  }

  if ( cropbox_dict[cropbox_mediabox].result ) {
    if ( ! pagedev->MediaBox )
      return TRUE ;
    if ( ! compare_boxes( cropbox_dict[cropbox_mediabox].result, pagedev->MediaBox ) )
      return TRUE ;
  }
  else {
    if ( pagedev->MediaBox )
      return TRUE ;
  }

  if ( cropbox_dict[cropbox_userunit].result ) {
    if ( ! pagedev->UserUnit )
      return TRUE;
    if ( object_numeric_value(cropbox_dict[cropbox_userunit].result) != object_numeric_value(pagedev->UserUnit) )
      return TRUE;
  }
  else {
    if ( pagedev->UserUnit )
      return TRUE ;
  }

  return FALSE ;
}

/* ---------------------------------------------------------------------- */

static Bool pdfattrs_readbox( OBJECT *box , char *title , uint8 ** ptr )
{
  HQASSERT( title , "title NULL in pdfattrs_readbox" ) ;
  HQASSERT( ptr , "ptr NULL in pdfattrs_readbox" ) ;
  HQASSERT( *ptr , "*ptr NULL in pdfattrs_readbox" ) ;

  if ( box ) {
    sbbox_t bbox ;

    if ( !object_get_bbox(box, &bbox) )
      return FALSE ;

    swcopyf( *ptr , (uint8 *)"%s [ %g %g %g %g ] ",
             title,
             bbox.x1, bbox.y1, bbox.x2, bbox.y2) ;
    *ptr += strlen( (char *)(*ptr ) ) ;
  }

  return TRUE ;
}

/* ---------------------------------------------------------------------- */

Bool pdf_setpagedevice( PDFXCONTEXT *pdfxc ,
                        PDF_PAGEDEV *pagedev ,
                        Bool *mbox_gt_inch,
                        SYSTEMVALUE *translation)
{
  /*
   * Large enough to accommodate the string with all the parameters.
   * Remember to check this if more values are added in the future.
   */
#define PAGEDEVICE_BUF_SIZE 8192
  uint8 buf[ PAGEDEVICE_BUF_SIZE ] ;
  uint8 *ptr ;
  PDF_IXC_PARAMS *ixc ;
  OBJECT *cropbox ;
  int32 rot ;
  SYSTEMVALUE width , height ;
  sbbox_t cropbbox ;

  HQASSERT( pdfxc , "pdfxc NULL in pdf_setpagedevice" ) ;
  HQASSERT( pagedev ,  "pagedev NULL in pdf_setpagedevice" ) ;
  HQASSERT( mbox_gt_inch , "mbox_gt_inch NULL in pdf_setpagedevice" ) ;

  PDF_GET_IXC( ixc ) ;

  *mbox_gt_inch = TRUE ;

  /* Determine what page rotation is required. */
  if ( pagedev->Rotate ) {
    if ( oType(*(pagedev->Rotate)) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    rot = oInteger( *(pagedev->Rotate) ) ;
    /* Must be a multiple of 90 degrees. */
    if ( ( rot % 90 ) != 0 )
      return error_handler( TYPECHECK ) ;
    /* Translates PDF rotate into PS rotate. */
    rot = (4-((rot%360)/90))%4 ;
  }
  else {
    rot = pdfxc->pdd_orientation ;
  }

  /* note that we use the following fact about the symbolic constants for
   * the boxes: if box1 < box2 then box1 "contains" box2.  The intended result
   * is that if we wish to "crop to" the artbox, but it isn't specified, we
   * should default to the "next enclosing" box that actually is there, and
   * so on down the line.  The order is artbox, trimbox, bleedbox, mediabox.
   */
  if (!ixc->SizePageToBoundingBox) /* Will clip to bbox rather than size */
    cropbox = pagedev->MediaBox;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_CROPBOX  && pagedev->CropBox != NULL)
    cropbox = pagedev->CropBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_ARTBOX && pagedev->ArtBox != NULL)
    cropbox = pagedev->ArtBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_TRIMBOX && pagedev->TrimBox != NULL)
    cropbox = pagedev->TrimBox ;
  else if (ixc->pagecropto >= PDF_PAGECROPTO_BLEEDBOX && pagedev->BleedBox != NULL)
    cropbox = pagedev->BleedBox ;
  else
    cropbox = pagedev->MediaBox ;

  if ( cropbox == NULL )
    return error_handler( UNDEFINED ) ;

  if ( !object_get_bbox(cropbox, &cropbbox) )
    return FALSE ;

  /* Always need to return the translation arguments */
  translation[0] = cropbbox.x1 ;
  translation[1] = cropbbox.y1 ;

  /* Only call setpagedevice operator when the page attributes requested
     are different from what is currently installed. */
  if (! pdf_dosetpagedevice( pagedev, rot ) )
    return TRUE;


  /* Start off the setpagedevice string */
  ptr = buf ;
  swcopyf( ptr , (uint8 *)"<< " ) ;
  ptr += strlen( (char*) ptr ) ;

  swcopyf( ptr , (uint8 *)"/CropBox << " ) ;
  ptr += strlen( (char *) ptr ) ;
  if ( ! pdfattrs_readbox( pagedev->ArtBox, "/ArtBox", &ptr ) ||
       ! pdfattrs_readbox( pagedev->TrimBox, "/TrimBox", &ptr ) ||
       ! pdfattrs_readbox( pagedev->BleedBox, "/BleedBox", &ptr ) ||
       ! pdfattrs_readbox( pagedev->MediaBox, "/MediaBox", &ptr ) ||
       ! pdfattrs_readbox( pagedev->CropBox, "/CropBox", &ptr ) )
    return FALSE ;

  swcopyf( ptr , (uint8 *)">> " ) ;
  ptr += strlen( (char *) ptr ) ;

  /* Map PDF [ lower-left x, lower-left y, upper-right x, upper-right y ]
     into PS [ width, height ] */
  width  = cropbbox.x2 - cropbbox.x1 ;
  height = cropbbox.y2 - cropbbox.y1 ;
  if ( width < 72.0f ) {
    *mbox_gt_inch = FALSE ;
    width = ( ixc->strictpdf ) ? 72.0f : width ;
  }

  if ( height < 72.0f ) {
    *mbox_gt_inch = FALSE ;
    height = ( ixc->strictpdf ) ? 72.0f : height ;
  }

  if (pagedev->UserUnit != NULL) {
    USERVALUE scale = (USERVALUE) object_numeric_value( pagedev->UserUnit );

    width *= scale;
    height *= scale;
  }

  swcopyf( ptr , (uint8 *)"/PageSize [ %g %g ] " , width , height ) ;

  ptr += strlen( (char*) ptr ) ;


  /* Put in the rotation */
  swcopyf( ptr , (uint8 *)"/Orientation %d " , rot ) ;
  ptr += strlen( (char*) ptr ) ;

  swcopyf( ptr ,  (uint8 *)">> setpagedevice" ) ;

  /* Execute the setpagedevice operator. */
  if ( ! run_ps_string( buf ) )
    return FALSE ;

  return TRUE ;
}

/** Called when a new pagedevice is made: allows us to keep our
    internal state in sync. Think of it as a private /Install
    procedure that's impossible to remove accidentally. */

Bool pdf_newpagedevice( void )
{
  PDFXCONTEXT *pdfxc ;

  for ( pdfxc = pdfin_xcontext_base ; pdfxc != NULL ;
        pdfxc = pdfxc->next ) {
    if ( ! pdf_rr_newpagedevice( pdfxc )) {
      return FALSE ;
    }
  }

  return TRUE ;
}

void pdf_getpagedevice( PDFXCONTEXT *pdfxc )
{
  /* Load page device defaults from gstate into pdf execution
   * context, for use with pdf_setpagedevice when providing 'missing'
   * settings
   */
  OBJECT *pdevdict = & theIgsDevicePageDict( gstateptr ) ;
  OBJECT *res ;
  int32 val ;

  HQASSERT( pdfxc , "pdfxc is NULL in pdf_getpagedevice" ) ;
  HQASSERT( oType(*pdevdict) == ODICTIONARY,
            "pdevdict not a dictionary in pdf_getpagedevice" ) ;

  /* Load orientation */
  val = 0 ; /* Default to use if not found, or invalid */
  res = fast_extract_hash_name( pdevdict , NAME_Orientation ) ;
  if ( res && oType(*res) == OINTEGER ) {
    val = oInteger( *res ) ;
    if ( val < 0 || val > 3 )
      val = 0 ;
  }
  pdfxc->pdd_orientation = val ;
}


/** \brief Used by \see pdf_get_resource() and \see pdf_get_resourceid(). */

static NAMETYPEMATCH resource_dict[] = {
/* 0 */ { NAME_null | OOPTIONAL ,     4 , { ODICTIONARY, OARRAY,
                                            OPACKEDARRAY, OINDIRECT }},
          DUMMY_END_MATCH
} ;


/** Given the name of a resource instance (e.g. /F0) of a particular
    resource type (e.g. /Font), this function searches the resource
    dictionaries for it. The \c res parameter contains the resource
    name on entry, and the instance found (if any) on exit.
*/

Bool pdf_get_resource( PDFCONTEXT *pdfc , uint16 name ,
                       OBJECT *inst , OBJECT **resobj )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  PDF_DICTLIST *ptr ;
  int32 cache_slot = RES_CACHE_NA ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( name == NAME_ProcSet ||
            name == NAME_Font ||
            name == NAME_ColorSpace ||
            name == NAME_XObject ||
            name == NAME_ExtGState ||
            name == NAME_Pattern ||
            name == NAME_Shading ||
            name == NAME_Properties ,
            "name not a valid type in pdf_get_resource." ) ;
  HQASSERT( inst || (name == NAME_ProcSet) ,
            "Need an inst if not looking for a proc "
            "set in pdf_get_resource." ) ;

  *resobj = NULL ;

  /* Override NAME_null in resource_dict (to e.g. /Fonts). */
  resource_dict[ 0 ].name = ( uint16 ) ( ( int32 ) name | OOPTIONAL ) ;

  /* Don't need inst if it's a NAME_ProcSet we're looking for. */
  if ( name != NAME_ProcSet )
    if ( oType(*inst) != ONAME )
      return error_handler( TYPECHECK ) ;

  /* Check the resource cache. */
  if ( name == NAME_ColorSpace ) {
    switch ( theINameNumber( oName(*inst))) {
    case NAME_DefaultGray :
      cache_slot = RES_DEFAULTGRAY ;
      break ;
    case NAME_DefaultRGB :
      cache_slot = RES_DEFAULTRGB ;
      break ;
    case NAME_DefaultCMYK :
      cache_slot = RES_DEFAULTCMYK ;
      break ;
    }
    if ( cache_slot != RES_CACHE_NA &&
         pdfc->resource_cache[ cache_slot ].valid ) {
      *resobj = pdfc->resource_cache[ cache_slot ].resource ;
      return TRUE ;
    }
  }

  ptr = pdfc->pdfenv ;

  while ( ptr ) {
    OBJECT *tmpres ;
    if ( ! pdf_dictmatch( pdfc , ptr->dict , resource_dict ))
      return FALSE ;

    tmpres = resource_dict[ 0 ].result ;
    if ( tmpres ) {
      /* All except NAME_ProcSet must be dictionaries. */
      if ( oType(*tmpres) == ODICTIONARY  &&  name != NAME_ProcSet ) {
        OBJECT *tmpinst ;
        tmpinst = fast_extract_hash( tmpres , inst ) ;
        if ( tmpinst != NULL ) {
          if ( oType(*tmpinst) == OINDIRECT ) {
            OBJECT *tmpval ;
            if ( ! pdf_lookupxref( pdfc , & tmpval ,
                                   oXRefID(*tmpinst) ,
                                   theGen(*tmpinst) ,
                                   FALSE ) )
              return FALSE ;
            if ( tmpval == NULL ||
                 oType(*tmpval) == ONULL )
              return error_handler( UNDEFINEDRESOURCE ) ;

            if ( cache_slot != RES_CACHE_NA ) {
              pdfc->resource_cache[ cache_slot ].resource = tmpval ;
              pdfc->resource_cache[ cache_slot ].valid = TRUE ;
            }

            *resobj = tmpval ;
            return TRUE ;
          }
          else if ( oType(*tmpinst) != ONULL ) {
            if ( ixc->strictpdf )
              return error_handler( TYPECHECK ) ;
            else {
              if ( cache_slot != RES_CACHE_NA ) {
                pdfc->resource_cache[ cache_slot ].resource = tmpinst ;
                pdfc->resource_cache[ cache_slot ].valid = TRUE ;
              }
              *resobj = tmpinst ;
              return TRUE ;
            }
          }
        }
      }
      /* NAME_ProcSet must be an array/packedarray. */
      else if ( name == NAME_ProcSet &&
              ( oType(*tmpres) == OARRAY ||
                oType(*tmpres) == OPACKEDARRAY ) ) {
        *resobj = tmpres ;
        return TRUE ;
      }
      else
        return error_handler( TYPECHECK ) ;
    }
    ptr = ptr->next ;
  }

  switch ( cache_slot ) {
  case RES_DEFAULTGRAY :
  case RES_DEFAULTRGB :
  case RES_DEFAULTCMYK :
    /* Did not necessarily expect the resource to be present. */
    return TRUE ;
  default :
    /* Got to the end of the list without finding it. */
    return error_handler( UNDEFINEDRESOURCE ) ;
  }
  /* not reached */
}

/* ----------------------------------------------------------------------------
 * pdf_get_resourceid()
 * Given the name ("id") of a resource instance (e.g. /F0) of a particular
 * resource type (e.g. /Font), this function searches the resource dictionaries
 * for it.
 */
Bool pdf_get_resourceid( PDFCONTEXT *pdfc , uint16 name ,
                         OBJECT *rsrc_inst , OBJECT *resobj )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  PDF_DICTLIST *ptr ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( name == NAME_Font ||
            name == NAME_ColorSpace ||
            name == NAME_XObject ||
            name == NAME_ExtGState ||
            name == NAME_Pattern ||
            name == NAME_Shading ||
            name == NAME_Properties ,
            "name not a valid type in pdf_get_resourceid." ) ;
  HQASSERT( rsrc_inst , "Need a resource instance in pdf_get_resourceid." ) ;

  /* Override NAME_null in resource_dict. */
  resource_dict[ 0 ].name = ( uint16 ) ( ( int32 ) name | OOPTIONAL ) ;

  if (oType(*rsrc_inst) != ONAME )
    return error_handler( TYPECHECK ) ;

  ptr = pdfc->pdfenv ;

  while ( ptr ) {
    OBJECT *res ;

    /* Obtain the resource sub-dictionary (e.g. the /Font << >>
       resources dict) */
    if ( ! pdf_dictmatch( pdfc , ptr->dict , resource_dict ))
      return FALSE ;

    res = resource_dict[ 0 ].result ;
    if (res) {
      OBJECT *inst ;

      /* All resource sub-dictionaries must be dictionaries. */
      if (oType(*res) != ODICTIONARY)
        return error_handler( TYPECHECK ) ;

      /* Look for the required resource instance (e.g. /F0 within the
         /Font resources) */
      inst = fast_extract_hash( res, rsrc_inst ) ;
      if ( inst != NULL ) {
        if (oType(*inst) == OINDIRECT ) {
          OBJECT *tmpval ;
          if (!pdf_lookupxref( pdfc, &tmpval, oXRefID(*inst),
                               theGen(*inst), FALSE ))
            return FALSE ;
          if (tmpval == NULL || oType(*tmpval) == ONULL )
            return error_handler( UNDEFINEDRESOURCE ) ;
          Copy( resobj, inst ) ;
          return TRUE ;
        }
        else if (oType(*inst) != ONULL) {
          if ( ixc->strictpdf )
            return error_handler( TYPECHECK ) ;

          Copy( resobj, inst );
          return TRUE;
        }
      }
    }
    ptr = ptr->next ;
  }
  /* Got to the end of the list without finding it. */
  return error_handler( UNDEFINEDRESOURCE ) ;
}

/* Log stripped */
