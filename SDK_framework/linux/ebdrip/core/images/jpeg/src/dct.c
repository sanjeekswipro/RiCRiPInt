/** \file
 * \ingroup jpeg
 *
 * $HopeName: COREjpeg!src:dct.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * DCT (JPEG) filter wrapper functions.
 */

#include "core.h"
#include "coreinit.h"
#include "swdevice.h"
#include "swoften.h"
#include "swerrors.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "monitor.h"
#include "namedef_.h"
#include "fileio.h"
#include "filterinfo.h"
#include "dct.h"
#include "dctimpl.h"
#include "gu_dct.h"
#include "hqmemset.h"
#include "timing.h"
#include "params.h"

static Bool init_hufftable_group(huffgroup_t * group,
                                 int32 maxsize)
{
  int32 i;

  group->last = maxsize;

  group->maxnum = maxsize;
  group->tables = mm_alloc( mm_pool_temp ,
                              sizeof(HUFFTABLE) * group->maxnum ,
                              MM_ALLOC_CLASS_DCT_BUFFER ) ;
  if (group->tables == NULL)
    return error_handler( VMERROR ) ;

  HqMemZero(group->tables, sizeof(HUFFTABLE) * maxsize);

  for (i = 0;i < maxsize; i++)
    group->reindex[i] = i;

  return TRUE;
}


/*
 * NOTES:
 *
 * See the JPEG Tech Spec for definitions of nomenclature and descriptions
 * of some of the algorithms.
 * The size of the buffer for both the encoding filter and decoding filter
 * is calculated by:
 *   columns * colors * rows_in_MDU
 * For an decoding filter this is not known until the SOF signal.
 * The filter code is re-enterant in the sense that the once the buffer is
 * full/empty, the FlushBuffer/FillBuffer routines save the filter state and
 * exit. On the next FlushBuffer/FillBuffer the routine must restore the state
 * and continue. The dct_status field in the DCTSTATE structure keeps track
 * of this.
 */

static Bool dctFilterInit( FILELIST *filter,
                           OBJECT *args ,
                           STACK *stack )
{
  uint8     *buff ;
  int32     bufflength ;
  DCTSTATE  *dctstate ;
  HUFFTABLE *huff ;
  COMPONENTINFO *ci ;
  OBJECT *hsamples ;
  OBJECT *vsamples ;
  OBJECT *quanttables ;
  OBJECT *hufftables ;
  int32     i ;
  int32 pop_args = 0 ;
  uint32 * dc_huffs;
  Hq32x2 file_pos ;
  Bool can_seek_on_file;
  FILELIST *flptr ;
  void *vpapi;
  enum {
    dct_filter_Columns, dct_filter_Rows, dct_filter_Colors,
    dct_filter_HSamples, dct_filter_VSamples, dct_filter_QuantTables,
    dct_filter_QFactor, dct_filter_HuffTables, dct_filter_ColorTransform,
    dct_filter_JPEGTables, dct_filter_dummy
  } ;

  static NAMETYPEMATCH thematch[dct_filter_dummy + 1] = {
    { NAME_Columns | OOPTIONAL , 1, { OINTEGER }},                 /* 0 */
    { NAME_Rows | OOPTIONAL , 1, { OINTEGER }},                    /* 1 */
    { NAME_Colors | OOPTIONAL , 1, { OINTEGER }},                  /* 2 */
    { NAME_HSamples | OOPTIONAL, 2, { OARRAY , OPACKEDARRAY}},     /* 3 */
    { NAME_VSamples | OOPTIONAL, 2, { OARRAY , OPACKEDARRAY}},     /* 4 */
    { NAME_QuantTables | OOPTIONAL, 2, { OARRAY , OPACKEDARRAY }}, /* 5 */
    { NAME_QFactor | OOPTIONAL, 2, { OREAL , OINTEGER }},          /* 6 */
    { NAME_HuffTables | OOPTIONAL, 2, { OARRAY , OPACKEDARRAY }},  /* 7 */
    { NAME_ColorTransform | OOPTIONAL, 1, { OINTEGER }},           /* 8 */
    { NAME_JPEGTables | OOPTIONAL, 1, { OFILE }},                  /* 9 */
    DUMMY_END_MATCH
  };

  HQASSERT(args != NULL || stack != NULL,
           "Arguments and stack should not both be empty") ;
  if ( ! args && !isEmpty(*stack) ) {
    args = theITop(stack) ;
    if ( oType(*args) == ODICTIONARY )
      pop_args = 1 ;
  }

  if ( args && oType(*args) == ODICTIONARY ) {
    if ( ! oCanRead(*oDict(*args)) &&
         ! object_access_override(oDict(*args)) )
      return error_handler( INVALIDACCESS ) ;
    if ( ! dictmatch( args , thematch ))
      return FALSE ;
    if ( ! FilterCheckArgs( filter , args ))
      return FALSE ;
    OCopy( theIParamDict( filter ), *args ) ;
  } else
    args = NULL ;

  can_seek_on_file = TRUE;
  /* Get underlying source/target if we have a stack supplied. */
  if ( stack ) {

    if ( theIStackSize(stack) < pop_args )
      return error_handler(STACKUNDERFLOW) ;

    if ( ! filter_target_or_source(filter, stackindex(pop_args, stack)) )
      return FALSE ;

    ++pop_args ;
  }

  flptr = theIUnderFile(filter) ;
  if ( !file_seekable(flptr) ||
       (*theIMyFilePos(flptr))(flptr, &file_pos) == EOF ||
       !Hq32x2IsZero(&file_pos) ) {
    can_seek_on_file = FALSE;
  }

  /* allocate a state structure */
  dctstate = ( DCTSTATE * ) mm_alloc( mm_pool_temp ,
                                      sizeof(DCTSTATE),
                                      MM_ALLOC_CLASS_DCT_STATE ) ;
  if ( dctstate == NULL )
    return error_handler( VMERROR ) ;

  theIFilterPrivate( filter ) = dctstate ;

  /* quick dirty initialization */
  HqMemZero(dctstate, sizeof(DCTSTATE));

  /* Check for an alternate jpeg implementation and use that in preference to
     HQN jpeg. Can't handle jpegs from tiff that have external tables. */
  if ( UserParams.AlternateJPEGImplementations &&
       thematch[dct_filter_JPEGTables].result == NULL &&
       thematch[dct_filter_QuantTables].result == NULL ) {
    if ( SwFindRDR(RDR_CLASS_API, RDR_API_JPEG, 20140317, &vpapi, NULL) == SW_RDR_SUCCESS ) {
      dctstate->jpeg_api = (sw_jpeg_api_20140317*)vpapi;
    }
  }

  /* assume baseline jpeg */
  dctstate->max_hufftables = MAXHUFFTABLES_BASELINE;

  /*for progressive mode*/
  dctstate->RSD = FALSE;
  dctstate->mode = edctmode_baselinescan;
  dctstate->info = NULL;
  dctstate->coeffs = NULL;
  dctstate->coeffs_base = NULL;
  dctstate->currinfo = &dctstate->default_info;
  dctstate->default_info.EOBrun = 0;
  dctstate->default_info.type = 0;
  dctstate->rejig = TRUE;
  dctstate->num_mdublocks = 0;
  dctstate->successive = FALSE;
  dctstate->seekable = can_seek_on_file;

  dctstate->columns = 0 ;
  dctstate->rows = 0 ;
  dctstate->colors = 1 ;
  hsamples = NULL ;
  vsamples = NULL ;
  quanttables = NULL ;
  dctstate->qfactor = 1.0f ;
  hufftables = NULL ;
  dctstate->colortransform = -1 ;
  dctstate->info_fetch = FALSE;

  dctstate->icc_profile_chunks = onothing ; /* Struct copy to set slot properties */

  if (args) {
    OBJECT *theo ;

    /* get stuff out of the the dictionary match structure */
    if (( theo = thematch[dct_filter_Columns].result ) != NULL )
      dctstate->columns = oInteger(*theo) ;
    else if ( isIOutputFile( filter ))
      return error_handler( UNDEFINED ) ;

    if (( theo = thematch[dct_filter_Rows].result ) != NULL )
      dctstate->rows = oInteger(*theo) ;
    else if ( isIOutputFile( filter ))
      return error_handler( UNDEFINED ) ;

    if (( theo = thematch[dct_filter_Colors].result ) != NULL )
      dctstate->colors = oInteger(*theo) ;
    else if ( isIOutputFile( filter))
      return error_handler( UNDEFINED ) ;

    hsamples = thematch[dct_filter_HSamples].result ;
    vsamples = thematch[dct_filter_VSamples].result ;
    quanttables = thematch[dct_filter_QuantTables].result ;

    if ((theo = thematch[dct_filter_QFactor].result) != NULL ) {
      if ( !object_get_real(thematch[dct_filter_QFactor].result,
                            &dctstate->qfactor) )
        return FALSE ;
    }

    hufftables = thematch[dct_filter_HuffTables].result ;

    if ((theo = thematch[dct_filter_ColorTransform].result) != NULL ) {
      if ( oInteger(*theo) < 0  || oInteger(*theo) > 2 )
        return error_handler( RANGECHECK ) ;
      else
        dctstate->colortransform = oInteger(*theo) ;
    }
  } else {
    if ( isIOutputFile( filter ))
      return error_handler( UNDEFINED ) ;
  }

  dctstate->current_row = 0 ;

  if ( !init_hufftable_group(&dctstate->dc_huff, MAXHUFFTABLES) ||
       !init_hufftable_group(&dctstate->ac_huff, MAXHUFFTABLES) )
    return FALSE;

  if ( isIInputFile( filter )) { /* a decode filter */
    /* the buffer cannot be allocated until the number of columns is known,
     * which is transmitted in the SOF parameters
     */
    theIBuffer( filter ) = NULL ;
    theIPtr( filter ) = NULL ;
    theIBufferSize( filter ) = 0 ;
    theICount( filter ) = 0 ;
    theIFilterState( filter ) = FILTER_INIT_STATE ;

    dctstate->dct_status = EXPECTING_SOI ;
    dctstate->restart_interval = 0 ;
    dctstate->num_qtables = 0 ;
    dctstate->dc_huff.num = 0 ;
    dctstate->ac_huff.num = 0 ;
    dctstate->rows_in_MDU = 0 ;
    dctstate->JPEGtables = NULL;

    if (args) {
      OBJECT *theo ;
      if ((theo = thematch[dct_filter_JPEGTables].result) != NULL ) {
        dctstate->JPEGtables = oFile(*theo);
      }
    }

    /* no further state needs to be initialised since the SOF, SOS
     * DHT and DQT codes will define them.
     */

    HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
    if ( pop_args > 0 )
      npop(pop_args, stack) ;

    dctstate->endblock = (dctstate->columns + 7)/8;

    return TRUE ;
  }


  /* Supply a default ColorTransform if necessary. */

  if ( dctstate->colortransform == -1 ) {
    if ( dctstate->colors == 3 )
      dctstate->colortransform = 1 ;
    else
      dctstate->colortransform = 0 ;
  }

  /* the encoding filter must be initialised with Adobe defaults */
  /* initialise the private part of the dct state structure */
  ci = dctstate->components ;
  dc_huffs = dctstate->default_info.dc_huff_number;
  for ( i = 0 ; i < dctstate->colors ; i++ ) {
    ci->id_num = (i + 1) ;
    /* assume sampling rate of 1 */
    ci->num_hsamples = 1 ;
    ci->num_vsamples = 1 ;
    /* assume the default quantization and huffman tables */
    ci->qtable_number = 0 ;
    dc_huffs[i] = 0 ;
    ci++ ;
  }
  if ( hsamples )
    /* unpack the horizontal sampling rate from the HSamples array */
    if ( ! unpack_samples_array( dctstate , hsamples , TRUE ))
      return FALSE ;
  if ( vsamples )
    /* unpack the vertical sampling rate from the VSamples array */
    if ( ! unpack_samples_array( dctstate , vsamples , FALSE ))
      return FALSE ;
  if ( quanttables ) {
    /* unpack the QuantTables array */
    if ( ! unpack_quant_array( dctstate, quanttables ))
      return FALSE ;
  } else {
    /* use the default quantization tables */
    dctstate->num_qtables = 1 ;
    dctstate->quanttables[0] = encoded_adobe_qtable ;
  }


  if ( hufftables ) {
    /* unpack the HuffTables array */
    if ( ! unpack_huff_array( dctstate, hufftables ))
      return FALSE ;
  } else {
    /* use the Adobe default huffman tables */
    huff = &dctstate->dc_huff.tables[0] ;
    huff->encoded_length = adobe_dc_huff_length ;
    huff->encoded_hufftable = encoded_adobe_dc_hufftable ;
    if ( ! make_huffman_table( huff , TRUE , TRUE ))
      return FALSE ;
    huff = &dctstate->ac_huff.tables[0] ;
    huff->encoded_length = adobe_ac_huff_length ;
    huff->encoded_hufftable = encoded_adobe_ac_hufftable ;
    if ( ! make_huffman_table( huff , FALSE , TRUE ))
      return FALSE ;
    dctstate->dc_huff.num = 1 ;
    dctstate->ac_huff.num = 1 ;
  }
  if ( dctstate->colortransform)
    if ( ! init_RGB_to_YUV_tables())
      return FALSE ;

  /* calculate the size of the MDU */
  dctstate->max_hsamples = 0 ;
  dctstate->max_vsamples = 0 ;
  dctstate->blocks_in_MDU = 0 ;
  ci = dctstate->components ;
  for ( i = 0 ; i < dctstate->colors ; i++ ) {
    /* continue to calculate the number of blocks in the MDU */
    dctstate->blocks_in_MDU = ( dctstate->blocks_in_MDU +
                                (ci->num_hsamples * ci->num_vsamples)) ;
    /* continue to evaluate the size of the MDU */
    if ((int32)ci->num_hsamples > (int32)dctstate->max_hsamples )
      dctstate->max_hsamples = ci->num_hsamples ;
    if ((int32)ci->num_vsamples > (int32)dctstate->max_vsamples )
      dctstate->max_vsamples = ci->num_vsamples ;
    ci++ ;
  }
  /* set the horizontal and vertical skips for each component */
  ci = dctstate->components ;
  for ( i = 0 ; i < dctstate->colors ; i++ , ci++ ) {
    ci->h_skip = ((int32)dctstate->max_hsamples / (int32)ci->num_hsamples) ;
    ci->v_skip = ((int32)dctstate->max_vsamples / (int32)ci->num_vsamples) ;
    ci->sample_size = ((int32)ci->h_skip * (int32)ci->v_skip) ;
    ci->sample_size2 = ((int32)ci->sample_size / 2) ;
  }

  dctstate->cols_in_MDU = (8 * (int32)dctstate->max_hsamples) ;
  dctstate->rows_in_MDU = (8 * (int32)dctstate->max_vsamples) ;
  if ( (int32)dctstate->blocks_in_MDU > 10 )   /* page 139 PS-L2 book */
    return error_handler( IOERROR ) ;

  dctstate->non_integral_ratio = check_non_integral_ratios( dctstate ) ;

  dctstate->bytes_in_scanline = dctstate->columns * dctstate->colors ;
  dctstate->dct_status = AT_START_OF_IMAGE ;


  /* initialise the filter structure */

  /* allocate enough space for a linebuffer one MDU deep */
  bufflength = dctstate->bytes_in_scanline * (int32)dctstate->rows_in_MDU ;
  buff = ( uint8 * )mm_alloc( mm_pool_temp ,
                              bufflength ,
                              MM_ALLOC_CLASS_DCT_BUFFER ) ;
  if ( buff == NULL )
    return error_handler( VMERROR ) ;

  theIBuffer( filter ) = buff ;
  theIPtr( filter ) = buff ;
  theIBufferSize( filter ) = bufflength ;
  theICount( filter ) = 0 ;
  theIFilterState( filter ) = FILTER_INIT_STATE ;

  HQASSERT(pop_args == 0 || stack != NULL, "Popping args but no stack") ;
  if ( pop_args > 0 )
    npop(pop_args, stack) ;

  return TRUE ;
}


void free_huff_table( HUFFTABLE *huff )
{
  if ( huff->encoded_hufftable &&
       huff->encoded_hufftable != encoded_adobe_ac_hufftable &&
       huff->encoded_hufftable != encoded_adobe_dc_hufftable ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->encoded_hufftable ,
             ( mm_size_t )huff->encoded_length * sizeof( uint32 )) ;
    huff->encoded_hufftable = NULL ;
  }
  if ( huff->hashtable ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->hashtable ,
             256 * sizeof( int32 )) ;
    huff->hashtable = NULL ;
  }
  if ( huff->code_lengths ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->code_lengths ,
             ( mm_size_t )huff->num_of_codes * sizeof( uint32 )) ;
    huff->code_lengths = NULL ;
  }
  if ( huff->codes ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->codes ,
             ( mm_size_t )huff->num_of_codes * sizeof( uint32 )) ;
    huff->codes = NULL ;
  }
  if ( huff->mincode ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->mincode ,
             16 * sizeof( uint32 )) ;
    huff->mincode = NULL ;
  }
  if ( huff->maxcode ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->maxcode ,
             16 * sizeof( uint32 )) ;
    huff->maxcode = NULL ;
  }
  if ( huff->valptr ) {
    mm_free( mm_pool_temp ,
             ( mm_addr_t )huff->valptr ,
             16 * sizeof( uint32 )) ;
    huff->valptr = NULL ;
  }
}

static void dctFilterDispose( FILELIST *filter )
{
  DCTSTATE *state ;

  HQASSERT( filter , "filter NULL in dctFilterDispose." ) ;

  state = theIFilterPrivate( filter ) ;

  if ( state ) {
    int32 i ;

    if ( state->jpeg_api != NULL && state->jpeg_api_data != NULL )
      state->jpeg_api->decompress_close(&state->jpeg_api_data);

    if (state->RSD) {
      FILELIST * ufptr, * rsd;

      /* we have an underlying private RSD. Remove it first. */
      rsd = theIUnderFile(filter);
      HQASSERT(isIRSDFilter(rsd),"dctFilterDispose: expecting an RSD");
      ufptr = theIUnderFile(rsd);
      (void)theIMyCloseFile(rsd)( rsd, CLOSE_EXPLICIT );

      theIUnderFile(filter) = ufptr;
      state->RSD = FALSE;
    }

    for ( i = 0 ; i < ( int32 )state->num_qtables ; i++ ) {
      if ( state->quanttables[ i ] &&
           state->quanttables[ i ] != encoded_adobe_qtable ) {
        mm_free( mm_pool_temp ,
                 ( mm_addr_t )state->quanttables[ i ] ,
                 ( mm_size_t )64 * sizeof( uint32 )) ;
        state->quanttables[ i ] = NULL ;
      }
    }
    for ( i = 0 ; i < ( int32 )state->num_mdublocks ; i++ ) {
      if ( state->components[ i ].mdu_block ) {
        mm_free( mm_pool_temp ,
                 ( mm_addr_t )state->components[ i ].mdu_block ,
                 64 * state->max_hsamples *
                      state->max_vsamples * sizeof( int32 )) ;
        state->components[ i ].mdu_block = NULL ;
      }
    }
    for ( i = 0 ; i < state->dc_huff.last ; i++ ) {
        free_huff_table( & state->dc_huff.tables[ i ]) ;
    }
    mm_free( mm_pool_temp ,
              ( mm_addr_t )state->dc_huff.tables ,
              ( mm_size_t )sizeof( HUFFTABLE ) * state->dc_huff.maxnum) ;

    for ( i = 0 ; i < state->ac_huff.last ; i++ ) {
        free_huff_table( & state->ac_huff.tables[ i ]) ;
    }
    mm_free( mm_pool_temp ,
              ( mm_addr_t )state->ac_huff.tables ,
              ( mm_size_t )sizeof( HUFFTABLE ) * state->ac_huff.maxnum) ;

    while (state->info) {
      scaninfo * next = state->info->next;
      mm_free( mm_pool_temp ,
               ( mm_addr_t )state->info ,
               ( mm_size_t )sizeof( scaninfo )) ;
      state->info = next;
    }
    if (state->coeffs_base) {
      mm_free( mm_pool_temp ,
               ( mm_addr_t )state->coeffs_base ,
               ( mm_size_t )state->coeffs_size) ;
      state->coeffs_base = NULL;
      state->coeffs = NULL;
    }

    if (oType(state->icc_profile_chunks) == OARRAY) {
      int k;
      OBJECT *ar;

      for (k = 0, ar = oArray(state->icc_profile_chunks);
           k < theLen(state->icc_profile_chunks);
           k++, ar++) {
        if ( oType(*ar) == OSTRING ) {
          mm_free( mm_pool_temp , oString(*ar), theLen(*ar));
          theTags(*ar) = ONOTHING;
        }
      }
      mm_free( mm_pool_temp , oArray(state->icc_profile_chunks),
               sizeof(OBJECT) * theLen(state->icc_profile_chunks));
      theTags(state->icc_profile_chunks) = ONOTHING;
    }
    mm_free( mm_pool_temp ,
             ( mm_addr_t )state ,
             sizeof( DCTSTATE )) ;
    theIFilterPrivate( filter ) = NULL ;
  }

  if ( theIBuffer( filter )) {
    if ( isIInputFile( filter ))
      mm_free( mm_pool_temp ,
               theIBuffer( filter ) - 1 ,
               theIBufferSize( filter ) + 1 ) ;
    else
      mm_free( mm_pool_temp ,
               theIBuffer( filter ) ,
               theIBufferSize( filter )) ;
    theIBuffer( filter ) = NULL ;
  }
}

static Bool dctEncodeBuffer( FILELIST *filter )
{
  FILELIST *flptr ;
  DCTSTATE *dctstate ;

  HQASSERT( filter , "filter NULL in dctEncodeBuffer." ) ;

  flptr = theIUnderFile( filter ) ;
  dctstate = theIFilterPrivate( filter ) ;

  HQASSERT( flptr , "flptr NULL in dctEncodeBuffer." ) ;
  HQASSERT( dctstate , "dctstate NULL in dctEncodeBuffer." ) ;

  switch ( dctstate->dct_status ) {
    case AT_START_OF_IMAGE :
      if ( ! isIOpenFile( flptr ))
        return error_handler( IOERROR ) ;

      if ( ! output_marker_code( SOI , flptr ))
        return FALSE ;
      /* output the adobe extension */
      if ( ! output_adobe_extension( filter , dctstate ))
        return FALSE ;
      /* output the quantization tables */
      if ( ! output_quanttables( filter ))
        return FALSE ;
      /* encode the image */
      if ( ! output_start_of_frame( filter ))
        return FALSE ;
      /* output the Huffman tables */
      if ( ! output_hufftables( filter ))
        return FALSE ;

      if ( ! output_scan( filter ))
        return FALSE ;

      /* fall through, since no data has been consumed yet from the filter
       * buffer.
       */
      if ( theICount( filter ) == 0 )
        dctstate->dct_status = OUTPUT_SCAN ;

    case OUTPUT_SCAN :
      /* check there is enough data in the buffer to do a complete scan */
      if ( theICount( filter ) == 0 )
        return( TRUE ) ;

      if ( ! isIOpenFile( flptr ))
        return error_handler( IOERROR ) ;

      if ( ! encode_scan( filter , dctstate ))
        return FALSE ;

      return TRUE ;

    case DONE_ALL_DATA :
      if ( isIClosing( filter )) {
        if ( ! isIOpenFile( flptr ))
          return error_handler( IOERROR ) ;

        if ( ! output_marker_code( EOI , flptr ))
          return FALSE ;
      }
      if ( theICount( filter ) == 0 ) /* no more data has been written */
        return TRUE ;
      /* the rows have been dealt with - trying to write more data is an error */
      return error_handler( IOERROR ) ;

    default:
      HQFAIL( "Unrecognised status in dctEncodeBuffer." ) ;
      return error_handler( IOERROR ) ;
  }
}

static Bool HQNCALL jpeg_api_source_cb(void *data, size_t *bytes_in_buffer,
                                       const uint8 **buffer)
{
  FILELIST *uflptr = (FILELIST*)data;

  if ( !EnsureNotEmptyFileBuff(uflptr) ) {
    *bytes_in_buffer = 0;
    *buffer = NULL;
    return FALSE;
  }

  *bytes_in_buffer = theICount(uflptr);
  *buffer = theIPtr(uflptr);

  theICount(uflptr) -= (int32)*bytes_in_buffer;
  theIPtr(uflptr) += *bytes_in_buffer;

  return TRUE;
}

static Bool do_dctDecodeBuffer( FILELIST *filter, int32 *ret_bytes )
{
  DCTSTATE *dctstate ;
  FILELIST *flptr ;
  FILELIST *filebackup = NULL;
  int32 bytes = 0 ;
  int32 code ;

  HQASSERT( filter , "filter NULL in dctDecodeBuffer." ) ;

  dctstate = theIFilterPrivate( filter ) ;

  /* Only use libjpeg when not matching via the image context API. */
  if ( dctstate->match == NULL && dctstate->jpeg_api != NULL ) {
    int len;

    if ( dctstate->jpeg_api_data == NULL ) {
      if ( !dctstate->jpeg_api->decompress_init(&dctstate->jpeg_api_data,
                                                jpeg_api_source_cb,
                                                theIUnderFile(filter),
                                                &len) )
        return error_handler(IOERROR);

      {
        uint8 *buff = ( uint8 * )mm_alloc( mm_pool_temp , len + 1 ,
                                           MM_ALLOC_CLASS_DCT_BUFFER ) ;
        if ( buff == NULL )
          return error_handler(VMERROR);
        buff++;
        theIBuffer(filter) = buff;
        theIPtr(filter) = buff;
        theIBufferSize(filter) = len;
      }
    }

    len = theIBufferSize(filter);
    dctstate->jpeg_api->decompress_read(dctstate->jpeg_api_data,
                                        theIBuffer(filter), &len);
    HQASSERT(len <= theIBufferSize(filter), "DCT filter buffer overrun");
    *ret_bytes = filter->count = len;

    return TRUE;
  }

  if (dctstate->JPEGtables) {
    /* decode the JPEGTables first if present */
    filebackup = theIUnderFile(filter);
    theIUnderFile(filter) = dctstate->JPEGtables;
  }

  flptr = theIUnderFile(filter);

  HQASSERT( flptr , "flptr NULL in dctDecodeBuffer." ) ;
  HQASSERT( dctstate , "dctstate NULL in dctDecodeBuffer." ) ;

  switch ( dctstate->dct_status ) {
  case EXPECTING_SOI :
    /* get the SOI marker - error if not found */
    if ( ! get_marker_code( &code , flptr ))
      return error_handler( IOERROR ) ;
    if ( code != SOI )
      return error_handler( IOERROR ) ;
    /* continue to read in the other markers */
    for (;;) {
      /* get the other markers */
      if ( ! get_marker_code( &code , flptr ))
        return error_handler( IOERROR ) ;
      switch ( code ) {
      case SOF0 : /* start of frame, baseline */
      case SOF1 : /* start of frame, extended seqential dct */
      case SOF2 : /* start of frame, progressive dct */
        if ( ! decode_SOF( filter , dctstate , code ))
          return error_handler( IOERROR ) ;
        break ;
      case DHT :  /* define Huffman tables */
        if ( ! decode_DHT( filter , dctstate ))
          return FALSE ;
        break ;
      case RST0 : case RST1 : case RST2 : case RST3 :
      case RST4 : case RST5 : case RST6 : case RST7 :
        /* restart interval - shouldn't get one here */
        return error_handler( IOERROR ) ;
        /*break ;*/
      case EOI :  /* premature end of image */
        if (dctstate->JPEGtables) {
          Hq32x2 file_pos;

          /* now reinstate the original file but rewind this one first */
          if ((*theIMyResetFile( dctstate->JPEGtables ))( dctstate->JPEGtables ) == EOF )
            return error_handler( IOERROR ) ;
          Hq32x2FromInt32(&file_pos, 0);
          if ((*theIMySetFilePos( dctstate->JPEGtables ))( dctstate->JPEGtables , &file_pos ) == EOF )
            return error_handler( IOERROR ) ;

          theIUnderFile(filter) = filebackup;
          dctstate->JPEGtables = NULL;
          *ret_bytes = 0 ;
          return do_dctDecodeBuffer( filter, ret_bytes );
        }
        *ret_bytes = 0 ;
        return error_handler( IOERROR ) ;
      case SOS :  /* start of scan */
        dctstate->dct_status = IN_SCAN ;
        if ( ! decode_SOS( filter , dctstate ))
          return error_handler( IOERROR ) ;
        if ( ! decode_scan( filter , dctstate , &bytes , TRUE ))
          return FALSE ;
        if ( dctstate->dct_status != EXPECTING_EOI ) {
          *ret_bytes = bytes ;
          return TRUE ;
        }
        goto RETURN_EXPECTING_EOI ;
      case DQT :  /* define quantization table */
        if ( ! decode_DQT( filter , dctstate ))
          return FALSE ;
        break ;
      case DRI :  /* define restart interval */
        if (dctstate->JPEGtables) {
          /* skip it if just parsing JPEGTables file */
          if ( ! skip_DRI( flptr ))
            return error_handler( IOERROR ) ;
        } else {
          if ( ! decode_DRI( flptr , dctstate ))
            return error_handler( IOERROR ) ;
        }
        break ;
      case APP0 : /* Application marker */
        if ( ! decode_APP0( flptr , dctstate ))
          return error_handler( IOERROR ) ;
        break ;
      case APP1 : /* Application marker (exif?) */
        if (dctstate->seekable) {
          /* we can't do exif for non-seekable files, and we don't layer an
             RSD on for performance reasons (we don't want all jpeg streams
             in a PS file to slow down to handle exif data that can't legally
             be present */
          if ( ! decode_APP1( flptr , dctstate ))
            return error_handler( IOERROR ) ;
        } else {
          if ( ! dct_skip_comment( flptr ))
            return error_handler( IOERROR ) ;
        }
        break ;
      case APP2 :
        {
          imagefilter_match_t *match ;
          OBJECT key = OBJECT_NOTVM_NAME(NAME_EmbeddedICCProfile, LITERAL) ;

          if (dctstate->seekable &&
              ((match = filter_info_match(&key, dctstate->match)) != NULL )) {
            if ( ! decode_APP2( flptr , dctstate ))
              return error_handler( IOERROR ) ;
          } else {
            if ( ! dct_skip_comment( flptr ))
              return error_handler( IOERROR ) ;
          }
        }
        break ;
      case APPD :
        if ( ! decode_APPD( flptr , dctstate ))
          return error_handler( IOERROR ) ;
        break ;
      case APPE : /* look for adobe extension */
        if ( ! decode_APPE( flptr , dctstate ))
          return error_handler( IOERROR ) ;
        break ;
      case DNL :  /* define number of lines - fall through */
      case COM :  /* comment */
        if ( ! dct_skip_comment( flptr ))
          return error_handler( IOERROR ) ;
        break ;
      default:
        if ( code >= APP0 && code <= APPF ) {
          if ( ! dct_skip_comment(flptr) )
            return error_handler( IOERROR ) ;
        }
        /* When not a known app segment marker don't assume valid segment length
         * in case it is arbitrary binary data - let the code look for the next
         * 0xff that may be the start of a subsequent valid marker.
         */
      }

      /* If we were scanning for tags, and we now have all of the tags we want,
         return early. */
      if ( dctstate->match != NULL && dctstate->match_done )
        return TRUE ;
    }
  case IN_SCAN :
    if ( dctstate->match != NULL ) {
      /*
       * If we are scanning for tags, then my reading of the JPG spec says
       * there are no more we can find once we hit scan data. And if we do
       * continue searching, it adds a high performance penalty as we end
       * up parsing the whole image twice. So bail-out of tag searching early
       * and return.
       */
      dctstate->match_done = TRUE;
      return TRUE;
    }
    if ( ! decode_scan( filter , dctstate , &bytes , FALSE ))
      return FALSE ;
    if ( dctstate->dct_status != EXPECTING_EOI ) {
      *ret_bytes = bytes ;
      return TRUE ;
    }
  case EXPECTING_EOI :
RETURN_EXPECTING_EOI :
    /* note that a multiple scan JPEG will have picked
       up the EOI marker from the internal RSD file already
    */
    if (dctstate->mode == edctmode_baselinescan) {
      if ( ! get_marker_code( &code , flptr ) ||
           (( code != EOI ) && (code != SOS)) )
        if (!dctstate->info_fetch)
          monitorf(UVS("%%%%[ Warning: Missing end of image marker in DCT file. ]%%%%\n"));
    }
    dctstate->dct_status = GOT_EOI;
    /* fall through */
  case GOT_EOI :
    *ret_bytes = -bytes ;
    return TRUE ;
  default:
    HQFAIL( "Unrecognised status in DCTDecodeBuffer." ) ;
    return error_handler( IOERROR ) ;
  }
}

static Bool dctDecodeBuffer( FILELIST *filter, int32 *ret_bytes )
{
  Bool ok;

  probe_begin(SW_TRACE_INTERPRET_JPEG, 0);
  ok = do_dctDecodeBuffer(filter, ret_bytes);
  probe_end(SW_TRACE_INTERPRET_JPEG, 0);
  return ok;
}

/* A minimal decode to determine values contained in DecodeInfo.
   The presence of decodeinfo in dctstate indicates to dctDecodeBuffer
   that it can return early after obtaining the info required. */
static Bool dctDecodeInfo(FILELIST *filter, imagefilter_match_t *match)
{
  DCTSTATE *dctstate;
  int32 ret_bytes;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  Bool result = FALSE ;

  HQASSERT(filter, "filter is null");
  HQASSERT(match, "No image filter match");

  /* No Alpha-masked image support, just type 1 for now. */
  object_store_integer(&value, 1) ;
  if ( filter_info_callback(match, NAME_ImageType, &value, &result) )
    return result ;

  /* Only used for Alpha-masked images. */
  object_store_integer(&value, 0) ; /* No interleave. */
  if ( filter_info_callback(match, NAME_InterleaveType, &value, &result) )
    return result ;

  /* Only used for Alpha-masked images. */
  if ( filter_info_callback(match, NAME_PreMult, &fnewobj, &result) )
    return result ;

  if ( filter_info_callback(match, NAME_MultipleDataSources, &fnewobj, &result) )
    return result ;

  file_store_object(&value, filter, LITERAL) ;
  if ( filter_info_callback(match, NAME_DataSource, &value, &result) )
    return result ;

  /* Need to examine contents of image. */
  dctstate = theIFilterPrivate(filter);
  dctstate->match = match;
  dctstate->match_done = FALSE ;

  /* suppress monitor warnings for the moment */
  dctstate->info_fetch = TRUE;

  do {
    result = dctDecodeBuffer(filter, &ret_bytes);
  } while ( result && !dctstate->match_done && ret_bytes > 0 ) ;

  dctstate->match = NULL;

  return result ;
}

/** \brief Test if a file stream is a JPEG image without advancing the file
    position. This just looks at the file signature. */
Bool jpeg_signature_test(FILELIST *flptr)
{
  uint8 *ptr ;
  int32 count ;

  if ( isIIOError(flptr) ||
       !isIInputFile(flptr) ||
       !isIOpenFile(flptr) ||
       !EnsureNotEmptyFileBuff(flptr) )
    return FALSE ; /* These are all reasons for it not to be a JPEG file */

  count = theICount(flptr) ;
  ptr = theIPtr(flptr) ;

  /* Start with SOI, then tables/misc, then SOF. Any marker may optionally be
     preceded by any number of fill bytes (0xFF). N.B. we do mean > 0 here. */
  while ( --count > 0 && *ptr++ == 0xff ) {
    if ( *ptr == SOI )
      return TRUE ;
  }

  return FALSE ; /* No SOI */
}

/** \brief Test if a file stream is a JPEG JFIF image without advancing the
    file position. This just looks at the file signature. */
Bool jfif_signature_test(FILELIST *flptr)
{
  uint8 *ptr ;
  int32 count ;

  if ( isIIOError(flptr) ||
       !isIInputFile(flptr) ||
       !isIOpenFile(flptr) ||
       !EnsureNotEmptyFileBuff(flptr) )
    return FALSE ; /* These are all reasons for it not to be a JPEG file */

  count = theICount(flptr) ;
  ptr = theIPtr(flptr) ;

  /* Start with SOI, then APP0 JFIF. Any marker may optionally be preceded by
     any number of fill bytes (0xFF). N.B. we do mean > 0 here. */
  while ( --count > 0 && *ptr++ == 0xff ) {
    if ( *ptr == SOI ) {
      --count ; ++ptr ;
      while ( --count > 0 && *ptr++ == 0xff ) {
        if ( *ptr == APP0 ) {
          return (count >= 8 && /* APP0, 2xLen, J, F, I, F, \0 */
                  (ptr[1] << 8 | ptr[2]) >= 16 &&
                  ptr[3] == 'J' && ptr[4] == 'F' && ptr[5] == 'I' &&
                  ptr[6] == 'F' && ptr[7] == '\0') ;
        }
      }
      return FALSE ; /* No APP0 */
    }
  }

  return FALSE ; /* No SOI */
}

static void dct_encode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* dct encode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("DCTEncode"),
                       FILTER_FLAG | WRITE_FLAG ,
                       0, NULL , 0 ,
                       FilterError,                          /* fillbuff */
                       FilterFlushBuff,                      /* flushbuff */
                       dctFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       dctFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       dctEncodeBuffer ,                     /* filterencode */
                       FilterDecodeError,                    /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;
}

static void dct_decode_filter(FILELIST *flptr)
{
  HQASSERT(flptr, "No filter to initialise") ;

  /* dct decode filter */
  init_filelist_struct(flptr ,
                       NAME_AND_LENGTH("DCTDecode") ,
                       FILTER_FLAG | READ_FLAG | EXPANDS_FLAG ,
                       0, NULL , 0 ,
                       FilterFillBuff,                       /* fillbuff */
                       FilterFlushBufError,                  /* flushbuff */
                       dctFilterInit,                        /* initfile */
                       FilterCloseFile,                      /* closefile */
                       dctFilterDispose,                     /* disposefile */
                       FilterBytes,                          /* bytesavail */
                       FilterReset,                          /* resetfile */
                       FilterPos,                            /* filepos */
                       FilterSetPos,                         /* setfilepos */
                       FilterFlushFile,                      /* flushfile */
                       FilterEncodeError,                    /* filterencode */
                       dctDecodeBuffer,                      /* filterdecode */
                       FilterLastError,                      /* lasterror */
                       -1, NULL, NULL, NULL ) ;

  /* A minimal decode to determine values contained in DecodeInfo. */
  theIFilterDecodeInfo(flptr) = dctDecodeInfo;
}

static Bool jpeg_swstart(struct SWSTART *params)
{
  FILELIST *flptr ;

  UNUSED_PARAM(struct SWSTART*, params) ;

  if ( (flptr = mm_alloc_static(sizeof(FILELIST) * 2)) == NULL )
    return FALSE ;

  dct_encode_filter(&flptr[0]) ;
  filter_standard_add(&flptr[0]) ;
  dct_decode_filter(&flptr[1]) ;
  filter_standard_add(&flptr[1]) ;

  return TRUE ;
}

IMPORT_INIT_C_GLOBALS(gu_dct)

void jpeg_C_globals(core_init_fns *fns)
{
  init_C_globals_gu_dct() ;
  fns->swstart = jpeg_swstart ;
}

/*
Log stripped */
/* EOF dct.c */
