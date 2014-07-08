/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:nohqscreen.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stubs for HQN advanced screening (HPS, HDS, HCS, HMS, encrypted screen
 * caches) when compiled out.
 */

#include "core.h"
#include "swerrors.h"

#include "gu_prscn.h"
#include "gu_hsl.h"
#include "hpscreen.h"


int32 accurateCellMultiple(corecontext_t *context,
                           SYSTEMVALUE uFreq, SYSTEMVALUE uAngle,
                           SYSTEMVALUE *rifreq, SYSTEMVALUE *riangle,
                           SYSTEMVALUE *rofreq, SYSTEMVALUE *roangle,
                           SYSTEMVALUE *rdfreq, Bool *optimized_angle,
                           Bool singlecell,
                           int32 willzeroadjust,
                           NAMECACHE *spotfun)
{
  UNUSED_PARAM(corecontext_t *, context) ;
  UNUSED_PARAM(SYSTEMVALUE, uFreq) ;
  UNUSED_PARAM(SYSTEMVALUE, uAngle) ;
  UNUSED_PARAM(SYSTEMVALUE *, rifreq) ;
  UNUSED_PARAM(SYSTEMVALUE *, riangle) ;
  UNUSED_PARAM(SYSTEMVALUE *, rofreq) ;
  UNUSED_PARAM(SYSTEMVALUE *, roangle) ;
  UNUSED_PARAM(SYSTEMVALUE *, rdfreq) ;
  UNUSED_PARAM(Bool*, optimized_angle);
  UNUSED_PARAM(Bool, singlecell) ;
  UNUSED_PARAM(int32, willzeroadjust) ;
  UNUSED_PARAM(NAMECACHE *, spotfun) ;

  /* No accurate screen in tolerance. Just indicate we should use a single
     cell. */
  return 1 ;
}

void phasehalftones0(CELLS **cells,
                     int32 number,
                     USERVALUE *uvals)
{
  UNUSED_PARAM(CELLS **, cells) ;
  UNUSED_PARAM(int32, number) ;
  UNUSED_PARAM(USERVALUE *, uvals) ;
}

Bool phasehalftones1(CELLS **cells,
                     int32 number,
                     int32 scms)
{
  UNUSED_PARAM(CELLS **, cells) ;
  UNUSED_PARAM(int32, number) ;
  UNUSED_PARAM(int32, scms) ;
  return error_handler(INVALIDACCESS) ;
}

Bool phasehalftones2(CELLS **cells ,
                     int32 number ,
                     int32 scms)
{
  UNUSED_PARAM(CELLS **, cells) ;
  UNUSED_PARAM(int32, number) ;
  UNUSED_PARAM(int32, scms) ;
  return error_handler(INVALIDACCESS) ;
}

Bool spatiallyOrderHalftones(CELLS **cells,
                             int32 number,
                             struct CHALFTONE *tmp_chalftone)
{
  UNUSED_PARAM(CELLS **, cells) ;
  UNUSED_PARAM(int32, number) ;
  UNUSED_PARAM(struct CHALFTONE *, tmp_chalftone);
  return error_handler(INVALIDACCESS) ;
}

Bool updateScreenProgress(void)
{
  return TRUE ;
}

Bool parse_HSL_parameters(corecontext_t *context,
                          GS_COLORinfo *colorInfo,
                          struct CHALFTONE *tmp_chalftone,
                          RESPI_PARAMS *respi_params,
                          RESPI_WEIGHTS *respi_weights,
                          NAMECACHE *sfcolor,
                          Bool maybeAPatternScreen,
                          NAMECACHE **underlying_spot_function_name ,
                          int32 *detail_name ,
                          int32 *detail_index ,
                          SYSTEMVALUE *freqv0adjust,
                          SFLOOKUP sflookup[],
                          int32 sflookup_entries)
{
  UNUSED_PARAM(corecontext_t *, context);
  UNUSED_PARAM(GS_COLORinfo *, colorInfo);
  UNUSED_PARAM(struct CHALFTONE *, tmp_chalftone);
  UNUSED_PARAM(RESPI_PARAMS *, respi_params);
  UNUSED_PARAM(RESPI_WEIGHTS *, respi_weights);
  UNUSED_PARAM(NAMECACHE *, sfcolor);
  UNUSED_PARAM(Bool, maybeAPatternScreen);
  UNUSED_PARAM(NAMECACHE **, underlying_spot_function_name) ;
  UNUSED_PARAM(int32 *, detail_name) ;
  UNUSED_PARAM(int32 *, detail_index) ;
  UNUSED_PARAM(SYSTEMVALUE *, freqv0adjust);
  UNUSED_PARAM(SFLOOKUP *, sflookup);
  UNUSED_PARAM(int32, sflookup_entries);
  return TRUE ;
}

void report_screen_start(corecontext_t *context,
                         NAMECACHE *htname,
                         NAMECACHE *sfname,
                         SYSTEMVALUE freqv,
                         SYSTEMVALUE anglv)
{
  UNUSED_PARAM(corecontext_t *, context) ;
  UNUSED_PARAM(NAMECACHE *, htname) ;
  UNUSED_PARAM(NAMECACHE *, sfname) ;
  UNUSED_PARAM(SYSTEMVALUE, freqv) ;
  UNUSED_PARAM(SYSTEMVALUE, anglv) ;
}

void report_screen_end(corecontext_t *context,
                       NAMECACHE *htname,
                       NAMECACHE *sfname,
                       SYSTEMVALUE freqv,
                       SYSTEMVALUE anglv,
                       SYSTEMVALUE devfreq,
                       SYSTEMVALUE freqerr,
                       SYSTEMVALUE anglerr)
{
  UNUSED_PARAM(corecontext_t *, context);
  UNUSED_PARAM(NAMECACHE *, htname);
  UNUSED_PARAM(NAMECACHE *, sfname);
  UNUSED_PARAM(SYSTEMVALUE, freqv);
  UNUSED_PARAM(SYSTEMVALUE, anglv);
  UNUSED_PARAM(SYSTEMVALUE, devfreq);
  UNUSED_PARAM(SYSTEMVALUE, freqerr);
  UNUSED_PARAM(SYSTEMVALUE, anglerr);
}

/* Encrypted screens are not supported. Trash the data passed to us, to
   prevent possible leaks. */
Bool encrypt_halftone(uint8 * buffer, int32 length,
                      uint8 pass[16], uint16 id_number, uint8 kind)
{
  UNUSED_PARAM(uint8 *, pass);
  UNUSED_PARAM(uint16, id_number);
  UNUSED_PARAM(uint8, kind);

  while ( length > 0 ) {
    *buffer++ = 0xff ;
    --length ;
  }

  return FALSE ;
}

void init_C_globals_gu_prscn(void)
{
}

void init_C_globals_hpscreen(void)
{
}

/* Log stripped */
