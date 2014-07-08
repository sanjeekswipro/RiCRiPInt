/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:std_filt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Declares and initialises the table of standard filters.
 */

#include "core.h"
#include "coreinit.h"
#include "swdevice.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcmp.h"
#include "objects.h"
#include "fileio.h"
#include "namedef_.h"

#include "ascii85.h"
#include "asciihex.h"
#include "eexec.h"
#include "flate.h"
#include "generic.h"
#include "strfilt.h"
#include "interleave.h"
#include "lzw.h"
#include "null.h"
#include "rsd.h"
#include "rsdstore.h"
#include "runlen.h"
#include "subfile.h"
#include "unifilter.h"

static FILELIST *std_filters = NULL, **std_filters_tail = &std_filters ;

/* Add a filelist structure to the end of the standard filter chain */
void filter_standard_add(FILELIST *filter)
{
  HQASSERT(filter->next == NULL,
           "Standard filter initialisation should have NULL next") ;

#if defined( ASSERT_BUILD )
  {
    FILELIST *std ;

    for ( std = std_filters ; std ; std = std->next ) {
      HQASSERT(std != filter, "Filter already added to standard") ;
      HQASSERT(HqMemCmp(theICList(std), theINLen(std),
                        theICList(filter), theINLen(filter)) != 0,
               "Duplicate name in standard filter list") ;
    }
  }
#endif

  *std_filters_tail = filter ;
  std_filters_tail = &filter->next ;
}

FILELIST *filter_standard_find( uint8 *name , int32 len )
{
  FILELIST *flptr ;

  HQASSERT( std_filters != NULL,
            "standard filters not initialised." ) ;

  for ( flptr = std_filters ; flptr != NULL ; flptr = flptr->next ) {
    if ( len == flptr->len &&
         HqMemCmp( name , len , flptr->clist , len ) == 0 )
      return flptr ;
  }

  return NULL ;
}

/* Return the opposite encode/decode filter name of the one given. */

Bool filter_standard_inverse(int32 filter_name_num, int32 *result)
{
  HQASSERT( result , "result NULL in filter_inverse." ) ;

  switch ( filter_name_num ) {
  case NAME_AESDecode:
    *result = NAME_AESEncode ;
    break ;
  case NAME_AESEncode:
    *result = NAME_AESDecode ;
    break ;
  case NAME_ASCII85Decode:
    *result = NAME_ASCII85Encode ;
    break ;
  case NAME_ASCII85Encode:
    *result = NAME_ASCII85Decode ;
    break ;
  case NAME_ASCIIHexDecode:
    *result = NAME_ASCIIHexEncode ;
    break ;
  case NAME_ASCIIHexEncode:
    *result = NAME_ASCIIHexDecode ;
    break ;
  case NAME_CCITTFaxDecode:
    *result = NAME_CCITTFaxEncode ;
    break ;
  case NAME_CCITTFaxEncode:
    *result = NAME_CCITTFaxDecode ;
    break ;
  case NAME_DCTDecode:
    *result = NAME_DCTEncode ;
    break ;
  case NAME_DCTEncode:
    *result = NAME_DCTDecode ;
    break ;
  case NAME_FlateDecode:
    *result = NAME_FlateEncode ;
    break ;
  case NAME_FlateEncode:
    *result = NAME_FlateDecode ;
    break ;
  case NAME_LZWDecode:
    *result = NAME_LZWEncode ;
    break ;
  case NAME_LZWEncode:
    *result = NAME_LZWDecode ;
    break ;
  case NAME_RC4Decode:
    *result = NAME_RC4Encode ;
    break ;
  case NAME_RC4Encode:
    *result = NAME_RC4Decode ;
    break ;
  case NAME_RunLengthDecode:
    *result = NAME_RunLengthEncode ;
    break ;
  case NAME_RunLengthEncode:
    *result = NAME_RunLengthDecode ;
    break ;
  case NAME_StreamDecode:
    *result = NAME_StreamEncode ;
    break ;
  case NAME_StreamEncode:
    *result = NAME_StreamDecode ;
    break ;
  default:
    HQFAIL( "Unpaired filter name in filter_inverse." ) ;
    return FALSE ;
  }

  return TRUE ;
}

/* Returns the object name for the supplied filter */

int32 filter_standard_name(FILELIST *flptr)
{
  int32 i;

  static int32 std_names[] = { NAME_AESEncode,
                               NAME_AESDecode,
                               NAME_ASCII85Encode,
                               NAME_ASCII85Decode,
                               NAME_ASCIIHexEncode,
                               NAME_ASCIIHexDecode,
                               NAME_CCITTFaxEncode,
                               NAME_CCITTFaxDecode,
                               NAME_DCTEncode,
                               NAME_DCTDecode,
                               NAME_FlateEncode,
                               NAME_FlateDecode,
                               NAME_JBIG2Decode,
                               NAME_LZWEncode,
                               NAME_LZWDecode,
                               NAME_PNGDecode,
                               NAME_RunLengthEncode,
                               NAME_RunLengthDecode,
                               NAME_StreamEncode,
                               NAME_StreamDecode,
                               NAME_RC4Encode,
                               NAME_RC4Decode };

  HQASSERT(flptr, "flptr NULL in filter_name");
  HQASSERT(isIFilter(flptr), "flptr not a filter in filter_name");

  for (i=0; i<NUM_ARRAY_ITEMS(std_names) ; i++ ) {
    int32 namenum = std_names[i] ;
    NAMECACHE *name = &system_names[namenum] ;

    if ( theINLen(name) == flptr->len &&
         HqMemCmp(theICList(name), theINLen(name),
                  flptr->clist, flptr->len) == 0 )
      return namenum ;
  }

  return -1;
}

static void init_C_globals_std_filt(void)
{
  std_filters = NULL;
  std_filters_tail = &std_filters ;
}

/* Allocate memory for standard filters in this product and initialise them */
#define NUMBER_OF_FILTERS 23

static Bool filter_swstart(struct SWSTART *params)
{
  FILELIST *flptr, *filters ;

  UNUSED_PARAM(struct SWSTART *, params)

  filters = flptr = mm_alloc_static(NUMBER_OF_FILTERS * sizeof(FILELIST)) ;
  if ( filters == NULL )
    return FALSE ;

  string_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 1 */

  string_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 2 */

  rsd_store_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 3 */
  rsd_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 4 */

  null_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 5 */

  subfile_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 6 */

  ascii85_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 7 */
  ascii85_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 8 */

  eexec_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 9 */
  eexec_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 10 */

  asciihex_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 11 */
  asciihex_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 12 */

  lzw_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 13 */
  lzw_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 14 */

  runlen_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 15 */
  runlen_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 16 */

  generic_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 17 */
  generic_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 18 */

  flate_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 19 */
  flate_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 20 */

  unicode_encode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 21 */

  interleave_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 22 */

  interleave_multi_decode_filter(flptr) ;
  filter_standard_add(flptr++) ;  /* 23 */

  HQASSERT( flptr - filters == NUMBER_OF_FILTERS ,
            "didn't allocate correct amount of memory for filters" ) ;

  /* Create root last so we force cleanup on success. */
  return rsd_storeinit() ;
}

static Bool filter_postboot(void)
{
  return rsd_storepostboot();
}

static void filter_finish(void)
{
  rsd_storefinish();
}

/* Declare global init functions here to avoid header inclusion
   nightmare. */
IMPORT_INIT_C_GLOBALS( filtops )
IMPORT_INIT_C_GLOBALS( rsdblist )
IMPORT_INIT_C_GLOBALS( rsdstore )
IMPORT_INIT_C_GLOBALS( tstream )

void filter_C_globals(core_init_fns *fns)
{
  init_C_globals_filtops() ;
  init_C_globals_rsdblist() ;
  init_C_globals_rsdstore() ;
  init_C_globals_std_filt() ;
  init_C_globals_tstream() ;

  fns->swstart = filter_swstart ;
  fns->postboot = filter_postboot ;
  fns->finish = filter_finish ;
}

/* ============================================================================
Log stripped */
