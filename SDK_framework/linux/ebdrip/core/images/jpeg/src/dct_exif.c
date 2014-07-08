/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:dct_exif.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation functions for DCT (JPEG) filter.
 */

#include "core.h"
#include "swoften.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swctype.h"
#include "objects.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "fileio.h"
#include "filterinfo.h"

#include "gu_dct.h"
#include "tiftypes.h"
#include "ifdreadr.h"
#include "tifftags.h"
#include "tifreadr.h"

#include "namedef_.h"
#include "objstack.h"

/*-------------------------------------------------------------*/

#define EXIF_LEN 5

Bool decode_APP1( FILELIST *flptr, DCTSTATE *dctstate )
{
  int32 len, bytesread = 0 ;
  uint8 buff[EXIF_LEN];
  Hq32x2 pos;
  uint32 number_images;

  if (( len = dct_get_16bit_num( flptr )) < 0 )
    return FALSE ;
  len -= 2;

  if ( len >= EXIF_LEN ) {

    if ( !dct_read_data(flptr, EXIF_LEN , len, &bytesread, buff))
      return FALSE;

    /* check for EXIF name */
    if (buff[0]=='E' && buff[1]=='x' && buff[2]=='i' &&
        buff[3]=='f' && buff[4]==0) {

      corecontext_t *corecontext = get_core_context();
      tiff_file_t *tifffile;
      tiff_reader_t*    p_reader;       /* The tiff reader */
      tiff_rational_t   x_resolution;
      tiff_rational_t   y_resolution;
      Bool              resundefined;
      ifd_ifdentry_t*   p_ifdentry;
      uint32            res_unit;
      double            x_res,y_res;
      OBJECT            value = OBJECT_NOTVM_NOTHING;

      /* get to the start of the EXIF data */
      if ( !dct_skip_data(flptr, 1, len, &bytesread))
        return FALSE;

      /* remember the position, so we can return there later */
      if ( (*theIMyFilePos(flptr))(flptr, &pos) == EOF )
        return FALSE;

      /* now put the result into the offsets array */
      HQASSERT(pos.high == 0,"fileposition > 32 bit");

      if ( !tiff_new_file(flptr, mm_pool_temp, &tifffile) )
        return FALSE;

      if ( !tiff_new_reader(corecontext, tifffile, mm_pool_temp, &p_reader) ) {
        tiff_free_file(&tifffile) ;
        return FALSE;
      }
      if ( tiff_read_header(corecontext, p_reader) &&
           ifd_read_ifds(ifd_reader_from_tiff(p_reader), &number_images) ) {

        if (tiff_set_image(p_reader, 1)) {

          if (tiff_check_exif(corecontext, p_reader)) {

            /* ResolutionUnit */
            resundefined = TRUE;
            p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader),
                                        TAG_ResolutionUnit);
            res_unit = (p_ifdentry != NULL)
              ? ifd_entry_unsigned_int(p_ifdentry)
              : ENTRY_DEF_RESOLUTION_UNITS;

            /* X and Y res are required entries so must be present */
            if ( (p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader),
                                              TAG_XResolution)) != NULL) {
              ifd_entry_rational(p_ifdentry, x_resolution);
              if ( (p_ifdentry = ifd_find_entry(tiff_current_ifd(p_reader),
                                                TAG_YResolution)) != NULL) {
                ifd_entry_rational(p_ifdentry, y_resolution);

                if (x_resolution[RATIONAL_NUMERATOR] &&
                    y_resolution[RATIONAL_NUMERATOR] &&
                    x_resolution[RATIONAL_DENOMINATOR] &&
                    y_resolution[RATIONAL_DENOMINATOR]){
                  x_res = ((double)x_resolution[RATIONAL_NUMERATOR])/
                    x_resolution[RATIONAL_DENOMINATOR];
                  y_res = ((double)y_resolution[RATIONAL_NUMERATOR])/
                    y_resolution[RATIONAL_DENOMINATOR];

                  /* Adjust based on units */
                  if ( res_unit == RESUNIT_CM ) {
                    x_res *= TIFF_CM_TO_INCH;
                    y_res *= TIFF_CM_TO_INCH;
                  }

                  object_store_real(&value, (float)x_res) ;
                  if ( filter_info_callback(dctstate->match, NAME_XResolution,
                                            &value, &dctstate->match_done) ) {
                    /* this will free tifffile too */
                    tiff_free_reader(&p_reader);
                    return dctstate->match_done ;
                  }
                  object_store_real(&value, (float)y_res) ;
                  if ( filter_info_callback(dctstate->match, NAME_YResolution,
                                            &value, &dctstate->match_done) ) {
                    /* this will free tifffile too */
                    tiff_free_reader(&p_reader);
                    return dctstate->match_done ;
                  }
                }
              }
            }
          }
        }
      }
      tiff_free_reader(&p_reader);    /* this will free tifffile too */
      if (error_signalled())
      {
        if (error_latest() == UNDEFINED)
          error_clear();
      }

      /* reset file to where we were */
      if (  (*theIMyResetFile(flptr))(flptr) == EOF ||
            (*theIMySetFilePos(flptr))(flptr, &pos) == EOF )
        return (*theIFileLastError(flptr))(flptr);
    }
  }

  /* skip the rest of the field */
  if ( !dct_skip_data(flptr, len-bytesread, len, &bytesread) ||
       error_signalled())
    return FALSE;
  return TRUE ;
}


/*
Log stripped */
