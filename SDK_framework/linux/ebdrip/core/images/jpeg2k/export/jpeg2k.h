/** \file
 * \ingroup jpeg2000
 *
 * $HopeName: HQNjpeg2k-kak6!export:jpeg2k.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * C functions calling to or from Kakadu's C++ code.
 */

#ifndef __JPEG2K_H__
#define __JPEG2K_H__

#ifdef __cplusplus
extern "C" {
#endif

struct FILELIST;            /* from COREfileio */

/** \defgroup jpeg2000 JPEG 2000.
    \ingroup images
    \{
 */
int32 HQNFILE_fread(/*@notnull@*/ /*@in@*/ struct FILELIST * file,
                    /*@notnull@*/ /*@out@*/ uint8 * buf,
                    int32 num_bytes);
int32 HQNFILE_fwrite(/*@notnull@*/ /*@in@*/ struct FILELIST * file,
                     /*@notnull@*/ /*@in@*/ const uint8 * buf,
                     int32 num_bytes);
int32 HQNFILE_fflush(/*@notnull@*/ /*@in@*/ struct FILELIST * file);
int32 HQNFILE_fclose(/*@notnull@*/ /*@in@*/ struct FILELIST * file);
Bool HQNFILE_fgetpos(/*@notnull@*/ /*@in@*/ struct FILELIST * file,
                     /*@notnull@*/ /*@out@*/ int32 * result);
Bool HQNFILE_fsetpos(/*@notnull@*/ /*@in@*/ struct FILELIST * file,
                     int32 offset);
Bool JPEG2k_error(/*@notnull@*/ /*@in@*/ uint8 * text);

Bool jpg2k_open(/*@notnull@*/ /*@in@*/ struct FILELIST* ofile,
                Bool override_cs,
                Bool scale_to_byte,
                size_t free_size,
                /*@notnull@*/ /*@out@*/ int32 * id,
                /*@notnull@*/ /*@out@*/ int32 * cols,
                /*@notnull@*/ /*@out@*/ int32 * rows,
                /*@notnull@*/ /*@out@*/ int32 * colors,
                /*@notnull@*/ /*@out@*/ int32 * bitdepth);
Bool jpg2k_fill(int32 id,
                /*@notnull@*/ /*@out@*/ uint8 ** pbuffer,
                /*@notnull@*/ /*@out@*/ int32 * currline);
void jpg2k_info_hasalpha(int32 id, int32 * chan);
Bool jpg2k_info_chromakey(int32 id, int32 * keyed, int32 * values);
Bool jpg2k_info_maxtilesize(int32 id, int32 * tilex, int32 * tiley);
Bool jpg2k_info_colorspace( int32 id,
                            /*@notnull@*/ /*@out@*/int32 *colorspace,
                            /*@notnull@*/ /*@out@*/int32 * alphachan,
                            /*@notnull@*/ /*@out@*/Bool  * premult,
                            /*@notnull@*/ /*@out@*/int32 * rangevals, /* used if Lab only */
                            /*@notnull@*/ /*@out@*/int32 * offsetvals,
                            /*@notnull@*/ /*@out@*/int32 * lum);
Bool jpg2k_info_resolution(int32 id,
                           /*@notnull@*/ /*@out@*/ SYSTEMVALUE *xres,
                           /*@notnull@*/ /*@out@*/ SYSTEMVALUE *yres) ;
void jpg2k_info_output(int32 id);
void jpg2k_close(int32 id);

/** \} */

#ifdef __cplusplus
}
#endif

#endif /* protection for multiple inclusion */


/* Log stripped */
