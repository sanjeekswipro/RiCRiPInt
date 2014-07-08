/** \file
 * \ingroup shfill
 *
 * $HopeName: SWv20!src:shading.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported functions for smooth shading.
 */

#ifndef __SHADING_H__
#define __SHADING_H__

#include "objects.h"  /* OBJECT */
#include "shadex.h"

#define isIShadingInfoIndexed(sinfo) ((sinfo)->base_index == GSC_SHFILL_INDEXED_BASE)

/** \brief Representation of DataSource key to allow progressive extraction
   of vertices */
struct SHADINGsource {
  SHADINGinfo *sinfo ;
  OBJECT odata ;
  int32 bitspercomp, bitspercoord, bitsperflag ;
  int32 nbitsleft ;
  uint32 value ; /* Store for bits fetched from source */
  SYSTEMVALUE *decode ;
} ;

/* Test if dictionary is shading dictionary */
Bool is_shadingdict(OBJECT *theo);

/* Data source extraction */
Bool more_mesh_data(SHADINGsource *src) ;
Bool get_mesh_value(SHADINGsource *src, int32 bits, SYSTEMVALUE *decode,
                    SYSTEMVALUE *svalue, int32 *ivalue) ;
Bool get_vertex_flag(SHADINGsource *src, int32 *flagptr) ;
Bool get_vertex_coords(SHADINGsource *src, SYSTEMVALUE *px, SYSTEMVALUE *py) ;
Bool get_vertex_color(SHADINGsource *src, SHADINGvertex *vtx) ;

#endif /* protection for multiple inclusion */



/* Log stripped */
