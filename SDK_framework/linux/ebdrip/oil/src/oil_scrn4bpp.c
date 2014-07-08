/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_scrn4bpp.c(EBDSDK_P.1) $
 */
/*! \file
 *  \ingroup OIL
 *  \brief Screening module for 4bpp
 *
 * 4bpp screening module.
 *
 */
#include "oil.h"
#include "pms_export.h"
#include "oil_htm.h"
#include "swrle.h"
#include <string.h>
#include <stdio.h>

extern HTI* g_4bppHTI[];

/** @brief Implementation of DoHalftone().
 *
 * This is the function which actually actions a request for halftoning.
 *
 * The RIP calls @c DoHalftone() when it wants the module to render a halftone
 * instance into a channel within the RIP's band buffers.
 *
 * In the case of this example, the halftoning is so simple that even if the
 * RIP were capable of asynchronous halftoning, we would still opt to
 * complete ours synchronously.
 *
 * \param[in]      instance       A pointer to an instance of the selected halftone screen.
 * \param[in,out]  request        A pointer to the structure containing the halftoning request.  This 
                                  structure also contains pointers to the raster channels which receive
                                  the halftoned data.
 * \return         Returns TRUE if the halftoning request is successfully completed, FALSE otherwise.
 */
HqBool RIPCALL do4bppHalftone(sw_htm_instance *instance,
                              const sw_htm_dohalftone_request *request)
{

  sw_htm_coord  lineY = request->first_line_y ;
  uint32  iLine ;

  int           i ;
  int32 x;
  HTI           *ourInst = NULL ;

  HQASSERT(NULL != request, "") ;
  HQASSERT(NULL != instance , "") ;

  HQASSERT(request->band_num >= 0 , "") ;
  HQASSERT(request->first_line_y >= 0 , "") ;
  HQASSERT(request->render_info->width > 0 , "") ;
  HQASSERT(request->render_info->height > 0 , "") ;
  HQASSERT(request->num_lines > 0 , "") ;
  HQASSERT(request->render_info->src_linebytes > 0 , "") ;
  HQASSERT(request->render_info->dst_linebytes > 0 , "") ;

  HQASSERT(request->num_src_channels == 1 , "") ;
  HQASSERT(NULL != request->src_channels , "") ;
  HQASSERT(request->num_dst_channels == 1 , "") ;
  HQASSERT(NULL != request->dst_channels , "") ;

  /* At the moment, the mask must always be present. */
  HQASSERT(NULL != request->msk_bitmap , "") ;

  /* Locate the instance in our list. If we had many entries and did not want
     to search all of them for the selected entry, we could subclass the
     instance, and put the screen index or a table pointer into the subclass.
     We would then downcast the instance pointer to the subclass and extract
     the screen index or table pointer in two lines of code. */
  for ( i = 0 ; i < 4 ; i++ )
  {  HTI *ptr = g_4bppHTI[ i ];
     if ( ptr->selected == instance )
     { ourInst = ptr ;
       break ;
     }
  }

  if (!ourInst)
  { request->DoneHalftone( request, SW_HTM_ERROR_BAD_HINSTANCE ) ;
    return FALSE ;
  }

#ifdef DEBUG_WRITE_OBJ_MAP
  if(!fileObjMap[colorant])
  {
      fileObjMap[colorant] = fopen(szFile[colorant], "a+");
  }
  if(fileObjMap[colorant])
  {
      if(colorant==0)
      {
          fileheight+=request->num_lines;
          printf("objmap: %d x %d\n", request->render_info->width, fileheight);
      }
      fwrite(request->object_props_map, 1, request->render_info->width * request->num_lines, fileObjMap[colorant]);
      fclose(fileObjMap[colorant]);
      fileObjMap[colorant] = NULL;
  }
  colorant++;
  if(colorant==4)
      colorant = 0;
#endif

  /* Now, for each line */
  for (iLine = 0; iLine < request->num_lines ; iLine++, lineY++ )
  {
    sw_htm_raster_ptr pMsk = request->msk_bitmap
                          + ( iLine * request->render_info->msk_linebytes
                              / sizeof(sw_htm_raster_unit) ) ;

    sw_htm_raster_ptr pDst = request->dst_channels[ 0 ]
                          + ( iLine * request->render_info->dst_linebytes
                              / sizeof(sw_htm_raster_unit) ) ;

    uint8* pSrc8 = (uint8*)request->src_channels[ 0 ]
                          + ( iLine *request->render_info->src_linebytes );
    uint8* pDest8 = (uint8*)pDst;

    uint8* pObjMap = (uint8*)request->object_props_map + ( iLine * request->render_info->src_linebytes );


    /* The index into the vertical axis of the cell for this line. */
    int32 celloffset;
    const uint8 **ppCell;
    int8 s;
    int32 scnSize;
    uint32 x32 = 0x80000000;
    uint8 uSrc = 0;
#ifdef LOWBYTEFIRST
    int8 anSwap[8] = { 3, 3, 1, 1, -1, -1, -3, -3 };
#endif

    for(x = 0; x < request->render_info->width; x++)
    {
        if(*pMsk & x32)
        {
#ifdef LOWBYTEFIRST
            pDest8 = (uint8*)pDst + (x>>1) + anSwap[x&7];
#else
            pDest8 = (uint8*)pDst + (x>>1);
#endif
            uSrc = pSrc8[x];

            /* clear the pixel */
            if(x&1)
               *pDest8 &= 0xF0;
            else
               *pDest8 &= 0x0F;

            if(pObjMap[x] & RLE_TEXT_OBJECT)
            {
                celloffset = ((lineY % ourInst->pHTables[GG_OBJ_TEXT].uHeight) * ourInst->pHTables[GG_OBJ_TEXT].uWidth)
                             + (x % ourInst->pHTables[GG_OBJ_TEXT].uWidth);
                ppCell = &(*ourInst->pHTables[GG_OBJ_TEXT].ditherMatrix)[14];
                scnSize = (ourInst->pHTables[GG_OBJ_TEXT].uWidth * ourInst->pHTables[GG_OBJ_TEXT].uHeight);
            }
            else if(pObjMap[x] & RLE_LW_OBJECT)
            {
                celloffset = ((lineY % ourInst->pHTables[GG_OBJ_GFX].uHeight) * ourInst->pHTables[GG_OBJ_GFX].uWidth)
                             + (x % ourInst->pHTables[GG_OBJ_GFX].uWidth);
                ppCell = &(*ourInst->pHTables[GG_OBJ_GFX].ditherMatrix)[14];
                /* Screen pattern size in memory is 8 byte aligned */
                scnSize = (ourInst->pHTables[GG_OBJ_GFX].uWidth * ourInst->pHTables[GG_OBJ_GFX].uHeight);
            }
            else /* RLE_VIGNETTE_OBJECT | RLE_IMAGE_OBJECT | RLE_COMPOSITED_OBJECT */
            {
                celloffset = ((lineY % ourInst->pHTables[GG_OBJ_IMAGE].uHeight) * ourInst->pHTables[GG_OBJ_IMAGE].uWidth)
                             + (x % ourInst->pHTables[GG_OBJ_IMAGE].uWidth);
                ppCell = &(*ourInst->pHTables[GG_OBJ_IMAGE].ditherMatrix)[14];
                /* Screen pattern size in memory is 8 byte aligned */
                scnSize = (ourInst->pHTables[GG_OBJ_IMAGE].uWidth * ourInst->pHTables[GG_OBJ_IMAGE].uHeight);
            }

#ifdef GG_OBJ_TYPES
            uaMaps[pObjMap[x]]++;
#endif

            if(uSrc==255)
            {
              if(x&1)
                 *pDest8 |= 0x0F;
              else
                 *pDest8 |= 0xF0;
            }
            else if(uSrc)
            {
              /* Start at the last (15th) threshold value, and work backwards until the source value
                 is greater than the threshold value. This level is the output level, 1 to 15. */
              for(s=15;s>0;s--)
              {
                  if(uSrc > (*ppCell)[celloffset])
                  {
                      if(x&1)
                        *pDest8 |= s;
                      else
                        *pDest8 |= ((s)<<4);
                      break;
                  }
                  ppCell--;
              }
            }
        }

        x32>>=1;
        if(x32==0)
        {
            x32 = 0x80000000;
            pMsk++;
        }

    }

  } /* for each line */

#ifdef GG_OBJ_TYPES
  for( x=0; x<256; x++)
  {
      if(uaMaps[x])
          printf("uaMaps[%d]=%d\n", x, uaMaps[x]);
  }
#endif

  request->DoneHalftone( request, SW_HTM_SUCCESS ) ;

  return TRUE;
}




