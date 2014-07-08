/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:dl_cell.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported functions for the compressed cell display list object type.
 */

#ifndef __DL_CELL_H__
#define __DL_CELL_H__

#include "displayt.h" /* CELL */
#include "dl_color.h" /* COLORANTINDEX */
#include "rbtree.h" /* RBT_ROOT */
#include "mm.h" /* mm_pool_t */

Bool cellallocate( /*@in@*/ mm_pool_t pool ,
                   /*@in@*/ dbbox_t *bbox , uint32 colorant_count ,
                   /*@dependent@*/ /*@null@*/ /*@in@*/ COLORANTINDEX *cis ,
                   /*@null@*/ /*@in@*/ RBT_ROOT *color_tree ,
                   /*@null@*/ /*@in@*/ RBT_ROOT *spot_tree ,
                   /*@in@*/ RBT_ROOT *length_tree ,
                   uint32 span_count ,
                   /*@out@*/ CELL **ccptr ) ;

Bool cellallocatedummycell( /*@in@*/  mm_pool_t pool ,
                            /*@in@*/  dbbox_t *bbox ,
                                      uint32 colorant_count ,
                            /*@out@*/ CELL **ccptr ) ;

size_t cellfootprint( /*@in@*/ /*@only@*/ CELL *cell ) ;

void cellfree( /*@in@*/ /*@only@*/ CELL *cell ) ;

Bool cellismono( /*@in@*/ CELL *cell ) ;

uint32 cellcolorantcount( /*@in@*/ CELL *cell ) ;

COLORANTINDEX *cellcolorantindices( /*@in@*/ CELL *cell ) ;

Bool cellmapcolorant( /*@in@*/ CELL *cell , COLORANTINDEX ci ,
                      uint32 *mapping ) ;

void cellbbox( /*@in@*/ CELL *cell , /*@out@*/ dbbox_t *bbox ) ;

void cellencodespan( /*@in@*/ CELL *cell ,
                     /*@null@*/ /*@in@*/ RBT_NODE *color ,
                     /*@null@*/ /*@in@*/ RBT_NODE *spot ,
                     Bool transparent ,
                     /*@in@*/ RBT_NODE *length ,
                     /*@in@*/ uint32 row ,
                     /*@in@*/ uint32 *bitcount ) ;

#if defined( ASSERT_BUILD )
void cellstreamdone( /*@in@*/ CELL *cell , uint32 bitcount ) ;
#else
#define cellstreamdone( _cell , _bitcount ) EMPTY_STATEMENT() ;
#endif


struct blit_color_t;
struct MODHTONE_REF;

void celldecodespan( /*@in@*/ CELL *cell ,
                     /*@out@*/ Bool *transparent ,
                     /*@out@*/ uint8 *length ,
                     /*@in@*/ uint32 row ,
                     /*@in@*/ uint32 *bitcount ,
                     /*@in@*/ const struct MODHTONE_REF *selected_mht,
                     /*@in@*/ struct blit_color_t *blit_color ) ;

Bool cellstartrow( CELL *cell , uint32 row , uint32 *bitcount ) ;

void cellfinishrow( CELL *cell , uint32 row , uint32 bitcount ) ;

void cellgetrowstart( CELL *cell , uint32 row , uint32 *bitcount ) ;

Bool addcelldisplay( DL_STATE *page, CELL *cell ) ;

Bool docell( /*@in@*/ DL_STATE *page, /*@in@*/ CELL *cell ) ;

Bool emptycell( /*@in@*/ const CELL *cell ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
