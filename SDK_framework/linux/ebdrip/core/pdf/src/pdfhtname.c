/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!src:pdfhtname.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF path name mapping implementation
 */

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "namedef_.h"

#include "swpdf.h"
#include "pdfhtname.h"

/* ---------------------------------------------------------------------- */

typedef struct sf_map_table_tag {
  int32 external ;
  int32 internal ;
} SF_MAP_TABLE ;

static SF_MAP_TABLE sf_map_table[] = {
  /* From Adobe spot function name to our internal equivalent. */
  {NAME_Round,             NAME_Euclidean},
  {NAME_Round,             NAME_EuclideanAdobe},
  {NAME_Diamond,           NAME_Diamond},
  {NAME_Ellipse,           NAME_Ellipse},
  {NAME_EllipseA,          NAME_EllipseA},
  {NAME_InvertedEllipseA,  NAME_InvertedEllipseA},
  {NAME_EllipseB,          NAME_EllipseB},
  {NAME_EllipseC,          NAME_EllipseCAdobe},
  {NAME_InvertedEllipseC,  NAME_InvertedEllipseC},
  {NAME_Line,              NAME_LineAdobe},
  {NAME_LineX,             NAME_LineX},
  {NAME_LineY,             NAME_LineY},
  {NAME_Square,            NAME_Square},
  {NAME_Cross,             NAME_Cross},
  {NAME_Rhomboid,          NAME_RhomboidAdobe},
  {NAME_DoubleDot,         NAME_DoubleDot},
  {NAME_InvertedDoubleDot, NAME_InvertedDoubleDot},
  {NAME_SimpleDot,         NAME_Round},
  {NAME_InvertedSimpleDot, NAME_InvertedSimpleDot},
  {NAME_CosineDot,         NAME_CosineDot},
  {NAME_Double,            NAME_Double},
  {NAME_InvertedDouble,    NAME_InvertedDouble},
} ;

static SF_MAP_TABLE sf_extensions_map_table[] = {
  /* Our own spot function extensions. */
  {NAME_Euclidean,         NAME_Euclidean},
  {NAME_Elliptical1,       NAME_Elliptical1},
  {NAME_Elliptical2,       NAME_Elliptical2},
  {NAME_Line90,            NAME_Line90},
  {NAME_Square1,           NAME_Square1},
  {NAME_Square2,           NAME_Square2},
  {NAME_EllipticalQ1,      NAME_EllipticalQ1},
  {NAME_EllipticalQ2,      NAME_EllipticalQ2},
  {NAME_EllipticalP,       NAME_EllipticalP},
  /* The following spot functions are not in the PDF spec at present,
   * but are likely candidates for inclusion by Adobe later on. */
  {NAME_InvertedEllipseB,  NAME_InvertedEllipseB},
  {NAME_EllipseB2,         NAME_EllipseB2},
  {NAME_InvertedEllipseB2, NAME_InvertedEllipseB2},
} ;

Bool pdf_mapinternalnametopdf( NAMECACHE * internal, OBJECT * external)
{
  int16 internal_namenumber;
  int32 index;
  HQASSERT( internal, "pdf_mapinternalnametopdf: NULL internal.");
  HQASSERT( external, "pdf_mapinternalnametopdf: NULL external.");
  internal_namenumber = internal->namenumber;
  for (index = 0; index < NUM_ARRAY_ITEMS(sf_map_table); index++ ) {
    if (internal_namenumber == sf_map_table[ index ].internal) {
      theTags(*external) = ONAME | LITERAL ;
      oName(*external) = system_names + sf_map_table[ index ].external;
      return TRUE;
    }
  }
  return FALSE;
}


Bool pdf_convertpdfnametointernal( OBJECT * nameobj )
{
  NAMECACHE *sfname ;
  int32 i , n = NUM_ARRAY_ITEMS( sf_map_table ) ;

  HQASSERT( nameobj , "pdf_convertpdfnametointernal : nameobj is NULL");
  HQASSERT( ( oType(*nameobj) == ONAME ) ,
            "pdf_convertpdfnametointernal : nameobj is not of type string ");

  sfname = oName(*nameobj) ;

  /* Look for spot function in list of standard PDF spot functions. */
  for ( i = 0 ; i < n ; ++i )
    if ( sfname == system_names + sf_map_table[i].external ) {
      /* Map Adobe's spot function name onto ours. */
      oName(*nameobj) = system_names + sf_map_table[i].internal ;
      break ;
    }
  if ( i == n ) {
    /* Got to the end of the list without finding it. */
    /* Is the spot function one of our extensions? */
    n = NUM_ARRAY_ITEMS( sf_extensions_map_table ) ;
    for ( i = 0 ; i < n ; ++i )
      if ( sfname == system_names + sf_extensions_map_table[i].external )
        /* No mapping required for extensions. */
        break ;
    if ( i == n )
      /* Not a standard spot function and not one of our extensions. */
      return error_handler( UNDEFINED ) ;
  }
  return TRUE;
}

/* end of file pdfhtname.c */

/* Log stripped */
