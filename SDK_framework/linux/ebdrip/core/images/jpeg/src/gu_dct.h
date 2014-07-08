/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:gu_dct.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Internal implementation interfaces for JPEG filters.
 */

#ifndef __GU_DCT_H__
#define __GU_DCT_H__

#include "dctimpl.h"
#include "objecth.h"
#include "fileioh.h"
#include "jpegapi.h"

struct COMPONENTINFO {
  uint32 id_num ;
  uint32 num_hsamples ;
  uint32 num_vsamples ;
  uint32 qtable_number ;
  uint32 h_skip ;
  uint32 v_skip ;
  uint32 sample_size ;
  uint32 sample_size2 ;
  int32 dc_prediction ;
  int32 *mdu_block;  /* component of the MDU - NOT super sampled */
  uint32 num_hsamples_old ;
  int32  coeffs_offset;
} ;

struct HUFFTABLE {
  /* encoded huffman table */
  int32    encoded_length ;
  uint32*  encoded_hufftable ;
  /* expanded huffman table */
  uint32   num_of_codes ;
  uint32*  code_lengths ;
  uint32*  codes ;
  /* sorted version of hufftable */
  uint32*  huffval ;
  uint32*  mincode ;
  int32*   maxcode ;
  uint32*  valptr ;
  int32*  hashtable;
  int32*  spilltable;
} ;

/**
 * JPG Bit buffer
 *
 * Input data is being sucked through a byte stream, but DCT code needs
 * to be able to access it as a bitstream. Therefore need a buffer for
 * bits accessed from an input stream.
 */
struct BITBUFFER
{
  int32 nbits; /**< number of bits in the bitbuffer */
  uint32 data; /**< bitbuffer data */
};

/**
 * JPG Scan info
 */
struct scaninfo
{
  uint32 comp_in_scan;
  uint32 interleave_order[4];
  uint32 dc_huff_number[4];
  uint32 ac_huff_number[4];
  uint32 index;
  uint32 EOBrun;

  struct BITBUFFER bbuf;    /**< input stream bit buffer */
  struct BITBUFFER a_bbuf;  /**< alternate bit buffer ? */

  /*successive mode data*/
  Hq32x2 SOSo; /*file position after SOS markers on each scan*/
  Hq32x2 cSOSo; /* working space for "skip/fetch cycle"*/

  int32  Ss;
  int32  Se;
  int32  Ah;
  int32  Al;

  uint32 compbits;  /* bit field tracking which colors scan is in */
  int32  type;

  struct scaninfo *next;
};

struct huffgroup_t {
  HUFFTABLE     *tables ;
  HUFFTABLE *   current;
  int32         reindex[MAXHUFFTABLES];
  int32         last;
  int32         maxnum;
  uint32        num ;
} ;

struct DCTSTATE {
  /* If present, an alternate jpeg implementation used in preference to HQN jpeg. */
  sw_jpeg_api_20140317 *jpeg_api;
  void          *jpeg_api_data;

  /* variables set by the filter dictionary */
  int32         rows ;
  int32         columns ;
  int32         colors ;
  USERVALUE     qfactor ;
  int32         colortransform;
  int32         mode;

  /* Presence indicates matching image tags. */
  imagefilter_match_t *match ;
  Bool match_done ;

  /* private state */
  int32         dct_status ;
  int32         restart_interval ;
  int32         restarts_to_go ;
  int32         next_restart_num ;
  uint32        max_hsamples ;
  uint32        max_vsamples ;
  uint32        non_integral_ratio ;
  uint32        num_qtables ;
  uint32        num_mdublocks ;
  uint32        rows_in_MDU ;
  uint32        cols_in_MDU ;
  uint32        blocks_in_MDU ;
  int32         bytes_in_scanline ;
  OBJECT        icc_profile_chunks;
  Bool          seekable;
  /* baseline tables */
  int32         max_hufftables;
  COMPONENTINFO components[4] ;
  QUANTTABLE    quanttables[4] ;
  uint32        nrows, ncols ;
  int32         sample_211 ;
  FILELIST *    JPEGtables;
  int32         compind;
  scaninfo      default_info;

  scaninfo *    currinfo;
  uint32        current_row ;
  int32         current_col;
  COMPONENTINFO * current_ci;

  /*successive mode data*/
  scaninfo *    info;        /* list of additional scans */
  uint32        coeffs_size; /* coefficient array (successive mode) */
  int32 *       coeffs_base; /* coefficient array (successive mode) */
  int32 *       coeffs;      /* coefficient array (successive mode) */
  Bool          RSD ;        /* TRUE if underlying file is a private RSD */
  Hq32x2        lastpos;     /*file position after last scan*/
  int32         h,v;
  int32         rejig;
  int32         successive;
  int32         subMDUs;
  int32         endblock;
  int32         maxrows;
  int32         rowsleft;
  huffgroup_t   dc_huff;
  huffgroup_t   ac_huff;

  Bool          info_fetch; /* true if calling from imagecontextinfo_ */
} ;

extern uint32 encoded_adobe_qtable[] ;

extern uint32 encoded_adobe_dc_hufftable[] ;
extern uint32 encoded_adobe_ac_hufftable[] ;
extern int32 adobe_dc_huff_length ;
extern int32 adobe_ac_huff_length ;


/* prototypes */

Bool unpack_samples_array(DCTSTATE *dctstate, OBJECT *sarray, Bool horiz);
Bool unpack_quant_array(DCTSTATE *dctstate, OBJECT *qarray);
Bool unpack_huff_array(DCTSTATE *dctstate, OBJECT *harray);
Bool check_non_integral_ratios(DCTSTATE *dctstate);
Bool output_quanttables(FILELIST *filter);
Bool output_start_of_frame(FILELIST *filter);
Bool output_hufftables(FILELIST *filter);
Bool output_scan(FILELIST *filter);
Bool init_RGB_to_YUV_tables(void);
Bool init_YUV_to_RGB_tables(void);
Bool encode_scan(FILELIST *filter, register DCTSTATE *dct);
Bool skip_comment(FILELIST *flptr);
Bool decode_APP0(FILELIST *flptr, DCTSTATE *dctstate);
Bool decode_APP1(FILELIST *flptr, DCTSTATE *dctstate);
Bool decode_APP2(FILELIST *flptr, DCTSTATE *dctstate);
Bool decode_APPD(FILELIST *flptr, DCTSTATE *dctstate);
Bool decode_APPE(FILELIST *flptr, DCTSTATE *dctstate);
Bool decode_SOF(FILELIST *filter, DCTSTATE *dctstate, int32 frame_code);
Bool decode_SOS(FILELIST *filter, DCTSTATE *dctstate);
Bool decode_DRI(FILELIST *flptr, DCTSTATE *dctstate);
Bool skip_DRI(FILELIST *flptr);
Bool decode_DQT(FILELIST *filter, DCTSTATE *dctstate);
Bool decode_DHT(FILELIST *filter, DCTSTATE *dctstate);
Bool decode_scan(FILELIST *filter, DCTSTATE *dct,
                 int32 *ret_bytes, Bool reset);
Bool get_marker_code(int32 *pcode, register FILELIST *flptr);


Bool output_marker_code(register int32 code, register FILELIST *flptr);
Bool output_adobe_extension(FILELIST *filter, DCTSTATE *dctstate);
Bool make_huffman_table(HUFFTABLE *hufftable, Bool dctable, Bool encoding);
void free_huff_table(HUFFTABLE *huff) ;
int32 dct_get_16bit_num( register FILELIST *flptr );
Bool dct_skip_comment( FILELIST *flptr );
Bool dct_read_data(FILELIST *flptr , int32 count , int32 len, int32 *readcount, uint8* buff);
Bool dct_skip_data(FILELIST *flptr , int32 count , int32 len, int32 *readcount);

#endif /* protection for multiple inclusion */


/* Log stripped */
